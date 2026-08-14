#include "core/JobManager.h"

#include "core/Paths.h"
#include "core/ProjectStore.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>
#include <utility>

namespace CyberSnapper {

JobManager::JobManager(QObject *parent) : QObject(parent) {}

JobManager::~JobManager() { shutdown(); }

QString JobManager::validate(const JobRequest &request) {
  if (!request.id.isEmpty() && request.id.size() > 128) return "Invalid job ID";
  if (request.urls.isEmpty()) return "At least one URL is required";
  if (request.urls.size() > 1000) return "A job may contain at most 1,000 URLs";
  if (request.profile.viewports.isEmpty()) return "At least one viewport is required";
  bool enabledViewport = false;
  for (const auto &viewport : request.profile.viewports) enabledViewport = enabledViewport || viewport.enabled;
  if (!enabledViewport) return "At least one viewport must be enabled";
  if (request.profile.engines.isEmpty()) return "At least one browser engine is required";
  if (request.profile.formats.isEmpty()) return "At least one output format is required";
  if (request.profile.captureMode == "element" && request.profile.elementSelector.trimmed().isEmpty()) {
    return "Element capture requires a CSS selector";
  }
  for (const auto &engine : request.profile.engines) {
    if (!QStringList{"chromium", "firefox", "webkit"}.contains(engine)) return "Unsupported browser engine: " + engine;
  }
  for (const auto &format : request.profile.formats) {
    if (!QStringList{"png", "webp", "avif", "pdf"}.contains(format)) return "Unsupported output format: " + format;
  }
  for (const auto &urlText : request.urls) {
    const QUrl url(urlText.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || !QStringList{"http", "https"}.contains(url.scheme().toLower()) ||
        url.host().isEmpty()) {
      return "Only explicit public HTTP and HTTPS URLs are supported: " + urlText;
    }
    if (!url.userInfo().isEmpty()) return "URLs containing embedded credentials are not supported";
  }
  if (request.profile.formats.contains("pdf") && !request.profile.engines.contains("chromium")) {
    return "PDF output requires Chromium";
  }
  return {};
}

QString JobManager::submit(ProjectStore *store, JobRequest request, QString *error) {
  if (!store || !store->isOpen()) {
    if (error) *error = "A writable project must be open";
    return {};
  }
  if (request.id.isEmpty()) request.id = newId();
  request.projectId = store->projectId();
  request.projectRoot = store->root();
  request.profile = profileFromJson(toJson(request.profile));
  const QString validationError = validate(request);
  if (!validationError.isEmpty()) {
    if (error) *error = validationError;
    return {};
  }
  if (!store->insertJob(request, error)) return {};
  m_queue.enqueue({store, request});
  publish(store, {{"type", "job_queued"}, {"jobId", request.id}, {"status", "queued"}});
  emit queueChanged(m_queue.size(), m_active.size());
  QTimer::singleShot(0, this, &JobManager::startNext);
  return request.id;
}

void JobManager::startNext() {
  if (m_shuttingDown) return;
  while (m_active.size() < m_maximumActive && !m_queue.isEmpty()) startJob(m_queue.dequeue());
  emit queueChanged(m_queue.size(), m_active.size());
}

void JobManager::startJob(const PendingJob &pending) {
  const QString worker = Paths::workerEntry();
  if (worker.isEmpty()) {
    failBeforeStart(pending.store, pending.request,
                    "Capture worker is not built. Run: npm run build:worker");
    QTimer::singleShot(0, this, &JobManager::startNext);
    return;
  }

  auto *active = new ActiveJob;
  active->store = pending.store;
  active->request = pending.request;
  active->process = new QProcess(this);
  m_active.insert(active->request.id, active);
  active->store->updateJob(active->request.id, "preparing");
  publish(active->store, {{"type", "job_preparing"}, {"jobId", active->request.id}, {"status", "preparing"}});
  const QJsonArray previousEvents = active->store->events(active->request.id);
  if (!previousEvents.isEmpty()) {
    active->fallbackSequence = previousEvents.last().toObject().value("sequence").toVariant().toLongLong();
  }

  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert("CYBERSNAPPER_PROJECT_ROOT", active->request.projectRoot);
  const QString browserCache = Paths::browserCacheDir();
  environment.insert("CYBERSNAPPER_BROWSER_CACHE", browserCache);
  environment.insert("PLAYWRIGHT_BROWSERS_PATH", browserCache);
  active->process->setProcessEnvironment(environment);
  active->process->setWorkingDirectory(active->request.projectRoot);
  active->process->setProgram(Paths::nodeExecutable());
  active->process->setArguments({worker, "--stdio"});
  active->process->setProcessChannelMode(QProcess::SeparateChannels);

  connect(active->process, &QProcess::started, this, [this, active] {
    const QJsonObject command{{"protocolVersion", 1}, {"command", "run"}, {"job", toJson(active->request)}};
    active->process->write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
  });
  connect(active->process, &QProcess::readyReadStandardOutput, this, [this, active] { readWorkerOutput(active); });
  connect(active->process, &QProcess::readyReadStandardError, this, [this, active] {
    const QString message = QString::fromUtf8(active->process->readAllStandardError()).trimmed();
    if (!message.isEmpty()) publish(active->store, {{"type", "worker_log"}, {"jobId", active->request.id}, {"message", message}});
  });
  connect(active->process, &QProcess::errorOccurred, this, [this, active](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      handleWorkerEvent(active, {{"type", "job_failed"}, {"jobId", active->request.id},
                                 {"message", "Could not start capture worker: " + active->process->errorString()}});
    }
  });
  connect(active->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, active](int code, QProcess::ExitStatus status) { finishProcess(active, code, status); });
  active->process->start();
}

void JobManager::readWorkerOutput(ActiveJob *active) {
  if (!active || !active->process) return;
  active->stdoutBuffer.append(active->process->readAllStandardOutput());
  while (true) {
    const qsizetype newline = active->stdoutBuffer.indexOf('\n');
    if (newline < 0) break;
    const QByteArray line = active->stdoutBuffer.left(newline).trimmed();
    active->stdoutBuffer.remove(0, newline + 1);
    if (line.isEmpty()) continue;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (!document.isObject()) {
      publish(active->store, {{"type", "worker_protocol_error"}, {"jobId", active->request.id},
                              {"message", error.errorString()}});
      continue;
    }
    handleWorkerEvent(active, document.object());
  }
}

void JobManager::handleWorkerEvent(ActiveJob *active, QJsonObject event) {
  if (!active || !active->store) return;
  event.insert("protocolVersion", 1);
  event.insert("jobId", active->request.id);
  // The agent owns event ordering. Worker-local sequence numbers are intentionally
  // remapped so preparatory events cannot be overwritten in SQLite.
  event.insert("sequence", ++active->fallbackSequence);
  if (!event.contains("timestamp")) event.insert("timestamp", utcNow());
  const QString type = event.value("type").toString();

  if (type == "job_started") {
    active->store->updateJob(active->request.id, "running");
  } else if (type == "artifact_completed") {
    active->store->insertArtifact(active->request.id, event.value("artifact").toObject());
    active->store->updateJob(active->request.id, "running", {}, 1, 0);
  } else if (type == "artifact_failed") {
    active->store->insertArtifact(active->request.id, event.value("artifact").toObject());
    active->store->updateJob(active->request.id, "running", {}, 0, 1);
  } else if (type == "comparison_completed") {
    active->store->insertComparison(event.value("comparison").toObject());
  } else if (type == "job_succeeded" || type == "job_partial" || type == "job_failed" || type == "job_cancelled") {
    const QString status = type == "job_succeeded" ? "succeeded" :
                           type == "job_partial" ? "partial" :
                           type == "job_cancelled" ? "cancelled" : "failed";
    active->terminalSeen = true;
    active->store->updateJob(active->request.id, status, event.value("message").toString());
  }
  active->store->appendEvent(active->request.id, event);
  emit eventPublished(active->store->projectId(), event);
}

void JobManager::finishProcess(ActiveJob *active, int exitCode, QProcess::ExitStatus exitStatus) {
  if (!active || !m_active.contains(active->request.id)) return;
  if (!active->terminalSeen) {
    const QString message = exitStatus == QProcess::CrashExit
        ? "Capture worker crashed"
        : QStringLiteral("Capture worker exited with code %1").arg(exitCode);
    handleWorkerEvent(active, {{"type", "job_failed"}, {"message", message}});
  }
  m_active.remove(active->request.id);
  active->process->deleteLater();
  delete active;
  emit queueChanged(m_queue.size(), m_active.size());
  QTimer::singleShot(0, this, &JobManager::startNext);
}

void JobManager::publish(ProjectStore *store, QJsonObject event) {
  if (!store) return;
  event.insert("protocolVersion", 1);
  if (!event.contains("timestamp")) event.insert("timestamp", utcNow());
  const QString jobId = event.value("jobId").toString();
  if (!jobId.isEmpty() && !event.contains("sequence")) {
    qint64 sequence = 1;
    const auto previous = store->events(jobId);
    if (!previous.isEmpty()) sequence = previous.last().toObject().value("sequence").toVariant().toLongLong() + 1;
    event.insert("sequence", sequence);
    store->appendEvent(jobId, event);
    if (ActiveJob *active = m_active.value(jobId, nullptr)) active->fallbackSequence = sequence;
  }
  emit eventPublished(store->projectId(), event);
}

void JobManager::failBeforeStart(ProjectStore *store, const JobRequest &request, const QString &message) {
  store->updateJob(request.id, "failed", message);
  publish(store, {{"type", "job_failed"}, {"jobId", request.id}, {"message", message}});
}

bool JobManager::cancel(const QString &jobId, QString *error) {
  QQueue<PendingJob> retained;
  bool removed = false;
  while (!m_queue.isEmpty()) {
    PendingJob pending = m_queue.dequeue();
    if (pending.request.id == jobId) {
      removed = true;
      pending.store->updateJob(jobId, "cancelled");
      publish(pending.store, {{"type", "job_cancelled"}, {"jobId", jobId}, {"message", "Cancelled before start"}});
    } else {
      retained.enqueue(pending);
    }
  }
  m_queue = retained;
  if (removed) {
    emit queueChanged(m_queue.size(), m_active.size());
    return true;
  }

  ActiveJob *active = m_active.value(jobId, nullptr);
  if (!active) {
    if (error) *error = "Job is not queued or running";
    return false;
  }
  active->store->updateJob(jobId, "cancelling");
  publish(active->store, {{"type", "job_cancelling"}, {"jobId", jobId}});
  active->process->write(QJsonDocument(QJsonObject{{"protocolVersion", 1}, {"command", "cancel"}, {"jobId", jobId}})
                             .toJson(QJsonDocument::Compact) + '\n');
  QTimer::singleShot(5000, active->process, [process = active->process] {
    if (process->state() != QProcess::NotRunning) process->terminate();
    QTimer::singleShot(2000, process, [process] {
      if (process->state() != QProcess::NotRunning) process->kill();
    });
  });
  return true;
}

QString JobManager::retry(ProjectStore *store, const QString &jobId, QString *error) {
  if (!store) {
    if (error) *error = "Project is not open";
    return {};
  }
  const QJsonObject original = store->job(jobId);
  if (original.isEmpty()) {
    if (error) *error = "Job not found";
    return {};
  }
  JobRequest request = jobRequestFromJson(original.value("request").toObject());
  request.id = newId();
  request.source = "retry";
  return submit(store, request, error);
}

bool JobManager::hasActiveJobs() const { return !m_active.isEmpty() || !m_queue.isEmpty(); }
int JobManager::queuedCount() const { return m_queue.size(); }
int JobManager::activeCount() const { return m_active.size(); }

void JobManager::setMaximumActiveJobs(int count) {
  m_maximumActive = qBound(1, count, 2);
  startNext();
}

void JobManager::shutdown() {
  if (m_shuttingDown) return;
  m_shuttingDown = true;
  while (!m_queue.isEmpty()) {
    PendingJob pending = m_queue.dequeue();
    pending.store->updateJob(pending.request.id, "interrupted", "Agent stopped before the job started");
    publish(pending.store, {{"type", "job_failed"}, {"jobId", pending.request.id},
                            {"message", "Agent stopped before the job started"}});
  }
  const QList<ActiveJob *> activeJobs = m_active.values();
  m_active.clear();
  for (ActiveJob *active : activeJobs) {
    disconnect(active->process, nullptr, this, nullptr);
    if (active->process->state() != QProcess::NotRunning) {
      active->process->terminate();
      if (!active->process->waitForFinished(2000)) active->process->kill();
    }
    active->store->updateJob(active->request.id, "interrupted", "Agent stopped before the job completed");
    active->process->deleteLater();
    delete active;
  }
  emit queueChanged(0, 0);
}

} // namespace CyberSnapper
