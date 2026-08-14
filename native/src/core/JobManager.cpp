#include "core/JobManager.h"

#include "core/Paths.h"
#include "core/ProjectStore.h"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>
#include <utility>

namespace CyberSnapper {

namespace {
constexpr qsizetype kMaximumWorkerLine = 16 * 1024 * 1024;
}

JobManager::JobManager(QObject *parent) : QObject(parent) {}

JobManager::~JobManager() { shutdown(); }

QString JobManager::validate(const JobRequest &request) {
  if (!request.id.isEmpty() && request.id.size() > 128) return "Invalid job ID";
  if (request.urls.isEmpty()) return "At least one URL is required";
  if (request.urls.size() > 1000) return "A job may contain at most 1,000 URLs";
  if (request.profile.viewports.isEmpty()) return "At least one viewport is required";
  bool enabledViewport = false;
  qint64 enabledViewports = 0;
  for (const auto &viewport : request.profile.viewports) {
    enabledViewport = enabledViewport || viewport.enabled;
    if (viewport.enabled) {
      ++enabledViewports;
      if (request.profile.captureMode == "viewport") {
        const long double pixels = static_cast<long double>(viewport.width) * viewport.height *
                                   viewport.deviceScaleFactor * viewport.deviceScaleFactor;
        if (pixels > 64000000.0L) return "A viewport capture may contain at most 64 million device pixels";
      }
    }
  }
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
  qint64 formatsAcrossEngines = 0;
  for (const auto &engine : request.profile.engines) {
    for (const auto &format : request.profile.formats) {
      if (format != "pdf" || engine == "chromium") ++formatsAcrossEngines;
    }
  }
  const qint64 totalArtifacts = request.urls.size() * enabledViewports * formatsAcrossEngines;
  if (totalArtifacts > 10000) return "A job may create at most 10,000 artifacts";
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
  const QJsonArray events = store->events(request.id);
  if (!events.isEmpty()) emit eventPublished(store->projectId(), events.first().toObject());
  emit queueChanged(m_queue.size(), m_active.size());
  QTimer::singleShot(0, this, &JobManager::startNext);
  return request.id;
}

bool JobManager::recover(ProjectStore *store, JobRequest request, QString *error) {
  if (!store || !store->isOpen() || request.id.isEmpty()) {
    if (error) *error = "A persisted job needs an open project and job ID";
    return false;
  }
  request.projectId = store->projectId();
  request.projectRoot = store->root();
  request.profile = profileFromJson(toJson(request.profile));
  const QString validationError = validate(request);
  if (!validationError.isEmpty()) {
    if (error) *error = validationError;
    return false;
  }
  m_queue.enqueue({store, request});
  emit queueChanged(m_queue.size(), m_active.size());
  QTimer::singleShot(0, this, &JobManager::startNext);
  return true;
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
  active->lastEventMs = QDateTime::currentMSecsSinceEpoch();
  m_active.insert(active->request.id, active);
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
    const QJsonObject command{{"protocolVersion", 2}, {"command", "run"}, {"job", toJson(active->request)}};
    active->process->write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
  });
  connect(active->process, &QProcess::readyReadStandardOutput, this, [this, active] { readWorkerOutput(active); });
  connect(active->process, &QProcess::readyReadStandardError, this, [this, active] {
    QString message = QString::fromUtf8(active->process->readAllStandardError()).trimmed();
    if (message.size() > 65536) message = message.left(65536) + "\n[worker log truncated]";
    if (!message.isEmpty()) publish(active->store, {{"type", "worker_log"}, {"jobId", active->request.id}, {"message", message}});
  });
  connect(active->process, &QProcess::errorOccurred, this, [this, active](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      handleWorkerEvent(active, {{"type", "job_failed"}, {"jobId", active->request.id},
                                 {"message", "Could not start capture worker: " + active->process->errorString()}});
      const QString jobId = active->request.id;
      QTimer::singleShot(0, this, [this, jobId] {
        if (ActiveJob *failed = m_active.value(jobId, nullptr)) {
          finishProcess(failed, -1, QProcess::CrashExit);
        }
      });
    }
  });
  connect(active->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, active](int code, QProcess::ExitStatus status) { finishProcess(active, code, status); });
  active->process->start();
  const QString jobId = active->request.id;
  QTimer::singleShot(30000, this, [this, jobId] { checkLiveness(jobId); });
}

void JobManager::readWorkerOutput(ActiveJob *active) {
  if (!active || !active->process) return;
  active->stdoutBuffer.append(active->process->readAllStandardOutput());
  while (true) {
    const qsizetype newline = active->stdoutBuffer.indexOf('\n');
    if (newline < 0) break;
    if (newline > kMaximumWorkerLine) {
      handleWorkerEvent(active, {{"type", "job_failed"},
                                 {"message", "Capture worker produced an oversized protocol message"}});
      active->process->kill();
      return;
    }
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
    if (document.object().value("protocolVersion").toInt() != 2) {
      handleWorkerEvent(active, {{"type", "job_failed"},
                                 {"message", "Capture worker used an unsupported protocol version"}});
      active->process->kill();
      return;
    }
    handleWorkerEvent(active, document.object());
  }
  if (active->stdoutBuffer.size() > kMaximumWorkerLine) {
    handleWorkerEvent(active, {{"type", "job_failed"},
                               {"message", "Capture worker protocol buffer exceeded 16 MiB"}});
    active->process->kill();
  }
}

void JobManager::handleWorkerEvent(ActiveJob *active, QJsonObject event) {
  if (!active || !active->store) return;
  active->lastEventMs = QDateTime::currentMSecsSinceEpoch();
  event.insert("protocolVersion", 2);
  event.insert("jobId", active->request.id);
  // The agent owns event ordering. Worker-local sequence numbers are intentionally
  // remapped so preparatory events cannot be overwritten in SQLite.
  event.insert("sequence", ++active->fallbackSequence);
  if (!event.contains("timestamp")) event.insert("timestamp", utcNow());
  const QString type = event.value("type").toString();
  if (type == "worker_heartbeat") return;

  if (type == "job_started") active->startedSeen = true;
  if (type == "job_succeeded" || type == "job_partial" || type == "job_failed" ||
      type == "job_cancelled" || type == "job_interrupted") {
    active->terminalSeen = true;
  }
  QString persistenceError;
  if (!active->store->applyWorkerEvent(active->request.id, event, &persistenceError)) {
    active->terminalSeen = true;
    emit eventPublished(active->store->projectId(), {{"type", "storage_error"},
                         {"jobId", active->request.id}, {"message", persistenceError}});
    if (active->process->state() != QProcess::NotRunning) active->process->kill();
    return;
  }
  emit eventPublished(active->store->projectId(), event);
}

void JobManager::checkLiveness(const QString &jobId) {
  ActiveJob *running = m_active.value(jobId, nullptr);
  if (!running || running->terminalSeen) return;
  const qint64 quietMs = QDateTime::currentMSecsSinceEpoch() - running->lastEventMs;
  const QString message = running->startedSeen
      ? "Capture worker stopped responding for more than 45 seconds"
      : "Capture worker did not start within 30 seconds";
  if ((!running->startedSeen && quietMs >= 30000) || (running->startedSeen && quietMs >= 45000)) {
    handleWorkerEvent(running, {{"type", "job_failed"}, {"message", message}});
    if (running->process->state() != QProcess::NotRunning) running->process->kill();
    else finishProcess(running, -1, QProcess::CrashExit);
    return;
  }
  QTimer::singleShot(15000, this, [this, jobId] { checkLiveness(jobId); });
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
  event.insert("protocolVersion", 2);
  if (!event.contains("timestamp")) event.insert("timestamp", utcNow());
  const QString jobId = event.value("jobId").toString();
  if (!jobId.isEmpty() && !event.contains("sequence")) {
    qint64 sequence = 1;
    const auto previous = store->events(jobId);
    if (!previous.isEmpty()) sequence = previous.last().toObject().value("sequence").toVariant().toLongLong() + 1;
    event.insert("sequence", sequence);
    QString persistenceError;
    if (!store->applyWorkerEvent(jobId, event, &persistenceError)) {
      emit eventPublished(store->projectId(), {{"type", "storage_error"}, {"jobId", jobId},
                                               {"message", persistenceError}});
      return;
    }
    if (ActiveJob *active = m_active.value(jobId, nullptr)) active->fallbackSequence = sequence;
  }
  emit eventPublished(store->projectId(), event);
}

void JobManager::failBeforeStart(ProjectStore *store, const JobRequest &request, const QString &message) {
  publish(store, {{"type", "job_failed"}, {"jobId", request.id}, {"message", message}});
}

bool JobManager::cancel(const QString &jobId, QString *error) {
  QQueue<PendingJob> retained;
  bool removed = false;
  while (!m_queue.isEmpty()) {
    PendingJob pending = m_queue.dequeue();
    if (pending.request.id == jobId) {
      removed = true;
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
  publish(active->store, {{"type", "job_cancelling"}, {"jobId", jobId}});
  active->process->write(QJsonDocument(QJsonObject{{"protocolVersion", 2}, {"command", "cancel"}, {"jobId", jobId}})
                             .toJson(QJsonDocument::Compact) + '\n');
  QTimer::singleShot(5000, active->process, [process = active->process] {
    if (process->state() != QProcess::NotRunning) process->terminate();
    QTimer::singleShot(2000, process, [process] {
      if (process->state() != QProcess::NotRunning) process->kill();
    });
  });
  return true;
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
    publish(pending.store, {{"type", "job_interrupted"}, {"jobId", pending.request.id},
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
    publish(active->store, {{"type", "job_interrupted"}, {"jobId", active->request.id},
                            {"message", "Agent stopped before the job completed"}});
    active->process->deleteLater();
    delete active;
  }
  emit queueChanged(0, 0);
}

} // namespace CyberSnapper
