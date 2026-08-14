#include "core/AgentService.h"

#include "core/Models.h"
#include "core/Paths.h"
#include "core/ProjectStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QThread>
#include <QUrl>

namespace CyberSnapper {

namespace {

bool terminalStatus(const QString &status) {
  return QStringList{"succeeded", "partial", "failed", "cancelled", "interrupted"}.contains(status);
}

QStringList strings(const QJsonValue &value) {
  QStringList result;
  for (const auto &item : value.toArray()) {
    const QString text = item.toString().trimmed();
    if (!text.isEmpty()) result.append(text);
  }
  return result;
}

QJsonArray asJson(const QStringList &values) {
  QJsonArray result;
  for (const auto &value : values) result.append(value);
  return result;
}

QString comparisonKey(const QJsonObject &artifact) {
  return artifact.value("url").toString() + "|" + artifact.value("engine").toString() + "|" +
         artifact.value("viewportId").toString() + "|" + artifact.value("captureMode").toString() +
         "|" + artifact.value("format").toString();
}

QString comparisonKey(const QString &url, const QString &engine, const Viewport &viewport,
                      const CaptureProfile &profile, const QString &format) {
  return url + "|" + engine + "|" + viewport.id + "|" + profile.captureMode + "|" + format;
}

} // namespace

AgentService::AgentService(QObject *parent)
    : QObject(parent),
      m_settings("CyberBrand", "CyberSnapper"),
      m_jobs(this),
      m_scheduler(&m_jobs, [this] { return stores(); }, this),
      m_rest(this) {
  connect(&m_jobs, &JobManager::eventPublished, this,
          [this](const QString &projectId, const QJsonObject &event) {
    QJsonObject data = event;
    data.insert("projectId", projectId);
    m_rest.publishEvent(data);
    emit eventPublished("job.event", data);
    if (QStringList{"job_succeeded", "job_partial", "job_failed"}.contains(event.value("type").toString())) {
      emit notificationRequested("CyberSnapper", event.value("type").toString().mid(4).replace('_', ' ') +
                                                    ": " + event.value("jobId").toString().left(8));
    }
  });
  connect(&m_scheduler, &Scheduler::scheduleEvent, this,
          [this](const QString &projectId, QJsonObject event) {
    event.insert("projectId", projectId);
    emit eventPublished("schedule.event", event);
  });
  connect(&m_jobs, &JobManager::queueChanged, this, [this](int queued, int active) {
    emit eventPublished("queue.changed", {{"queued", queued}, {"active", active}});
  });
  connect(&m_rest, &RestServer::serverError, this, [this](const QString &message) {
    emit eventPublished("api.error", {{"message", message}});
  });
}

AgentService::~AgentService() { shutdown(); }

bool AgentService::start(QString *error) {
  if (m_started) return true;
  QStringList roots = m_settings.value("projects/recentRoots").toStringList();
  if (!roots.contains(Paths::defaultProjectRoot())) roots.append(Paths::defaultProjectRoot());
  QString firstError;
  for (const auto &root : roots) {
    if (root.trimmed().isEmpty()) continue;
    QString openError;
    if (openProject(root, {}, &openError)) continue;
    if (firstError.isEmpty()) firstError = openError;
  }
  if (m_projects.isEmpty()) {
    if (error) *error = firstError.isEmpty() ? "Could not open the default project" : firstError;
    return false;
  }
  const QString storedActive = m_settings.value("projects/activeId").toString();
  if (m_projects.contains(storedActive)) m_activeProjectId = storedActive;
  m_jobs.setMaximumActiveJobs(m_settings.value("jobs/maximumActive", 1).toInt());
  m_scheduler.start();
  if (m_settings.value("api/enabled", false).toBool()) {
    QString apiError;
    if (!configureApi(true, nullptr, &apiError)) {
      emit eventPublished("api.error", {{"message", apiError}});
    }
  }
  m_started = true;
  return true;
}

void AgentService::shutdown() {
  if (!m_started && m_projects.isEmpty()) return;
  m_scheduler.stop();
  m_rest.stop();
  m_jobs.shutdown();
  m_projects.clear();
  m_started = false;
}

QList<ProjectStore *> AgentService::stores() const {
  QList<ProjectStore *> result;
  result.reserve(m_projects.size());
  for (const auto &store : m_projects) result.append(store.get());
  return result;
}

ProjectStore *AgentService::project(const QString &projectId) const {
  const QString wanted = projectId.isEmpty() ? m_activeProjectId : projectId;
  const auto iterator = m_projects.constFind(wanted);
  return iterator == m_projects.constEnd() ? nullptr : iterator.value().get();
}

ProjectStore *AgentService::projectForJob(const QString &jobId) const {
  for (ProjectStore *store : stores()) {
    if (!store->job(jobId).isEmpty()) return store;
  }
  return nullptr;
}

ProjectStore *AgentService::projectForArtifact(const QString &artifactId) const {
  for (ProjectStore *store : stores()) {
    if (!store->artifact(artifactId).isEmpty()) return store;
  }
  return nullptr;
}

ProjectStore *AgentService::openProject(const QString &root, const QString &name, QString *error) {
  const QString absoluteRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
  for (const auto &existing : m_projects) {
    if (existing->root() == absoluteRoot) {
      m_activeProjectId = existing->projectId();
      return existing.get();
    }
  }
  auto store = std::make_shared<ProjectStore>();
  if (!store->open(absoluteRoot, name, error)) return nullptr;
  ProjectStore *result = store.get();
  m_projects.insert(store->projectId(), std::move(store));
  m_activeProjectId = result->projectId();
  m_settings.setValue("projects/activeId", m_activeProjectId);
  rememberProject(absoluteRoot);
  return result;
}

void AgentService::rememberProject(const QString &root) {
  QStringList roots = m_settings.value("projects/recentRoots").toStringList();
  roots.removeAll(root);
  roots.prepend(root);
  while (roots.size() > 12) roots.removeLast();
  m_settings.setValue("projects/recentRoots", roots);
}

QByteArray AgentService::tokenHash(const QString &token) {
  return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex();
}

QString AgentService::generateToken() {
  QByteArray bytes;
  bytes.reserve(32);
  for (int word = 0; word < 4; ++word) {
    const quint64 value = QRandomGenerator::system()->generate64();
    for (int shift = 0; shift < 64; shift += 8) bytes.append(char((value >> shift) & 0xff));
  }
  return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool AgentService::configureApi(bool enabled, QString *plainToken, QString *error) {
  if (!enabled) {
    m_rest.stop();
    m_settings.setValue("api/enabled", false);
    return true;
  }
  QByteArray hash = m_settings.value("api/tokenHash").toByteArray();
  if (hash.isEmpty()) {
    const QString token = generateToken();
    hash = tokenHash(token);
    m_settings.setValue("api/tokenHash", hash);
    if (plainToken) *plainToken = token;
  }
  const quint16 desiredPort = quint16(qBound(1024, m_settings.value("api/port", 39071).toInt(), 65535));
  if (!m_rest.isRunning()) {
    if (!m_rest.start(desiredPort, hash,
                      [this](const QString &method, const QJsonObject &params) {
                        return invokeThreadSafe(method, params);
                      }, error)) {
      return false;
    }
  }
  m_settings.setValue("api/enabled", true);
  m_settings.setValue("api/port", m_rest.port());
  return true;
}

QJsonObject AgentService::failure(const QString &code, const QString &message, int status) {
  return {{"_error", QJsonObject{{"code", code}, {"message", message}, {"status", status}}}};
}

QJsonObject AgentService::invokeThreadSafe(const QString &method, const QJsonObject &params) {
  if (QThread::currentThread() == thread()) return handle(method, params);
  QJsonObject result;
  QMetaObject::invokeMethod(this, [&] { result = handle(method, params); }, Qt::BlockingQueuedConnection);
  return result;
}

QJsonObject AgentService::handle(const QString &method, const QJsonObject &params) {
  if (method == "agent.ping") {
    return {{"ok", true}, {"version", CYBERSNAPPER_VERSION}, {"protocolVersion", 1}};
  }
  if (method == "agent.status") {
    return {{"version", CYBERSNAPPER_VERSION},
            {"activeProjectId", m_activeProjectId},
            {"queuedJobs", m_jobs.queuedCount()},
            {"activeJobs", m_jobs.activeCount()},
            {"api", QJsonObject{{"enabled", m_rest.isRunning()}, {"port", int(m_rest.port())}}}};
  }
  if (method == "agent.stop") {
    if (m_jobs.hasActiveJobs() && !params.value("force").toBool(false)) {
      return failure("jobs_active", "Jobs are active. Pass force=true to stop the agent.", 409);
    }
    QMetaObject::invokeMethod(this, [this] { emit quitRequested(); }, Qt::QueuedConnection);
    return {{"stopping", true}};
  }
  if (method == "project.list") {
    QJsonArray projects;
    for (ProjectStore *store : stores()) {
      projects.append(QJsonObject{{"id", store->projectId()}, {"name", store->projectName()},
                                  {"root", store->root()}, {"active", store->projectId() == m_activeProjectId}});
    }
    return {{"projects", projects}, {"activeProjectId", m_activeProjectId}};
  }
  if (method == "project.open" || method == "project.create") {
    const QString root = params.value("root").toString().trimmed();
    if (root.isEmpty()) return failure("invalid_project", "A project folder is required");
    QString error;
    ProjectStore *store = openProject(root, params.value("name").toString(), &error);
    if (!store) return failure("project_open_failed", error, 409);
    emit eventPublished("project.changed", {{"projectId", store->projectId()}});
    return {{"project", QJsonObject{{"id", store->projectId()}, {"name", store->projectName()},
                                     {"root", store->root()}}}};
  }
  if (method == "project.setActive") {
    ProjectStore *store = project(params.value("projectId").toString());
    if (!store) return failure("not_found", "Project not found", 404);
    m_activeProjectId = store->projectId();
    m_settings.setValue("projects/activeId", m_activeProjectId);
    emit eventPublished("project.changed", {{"projectId", m_activeProjectId}});
    return {{"activeProjectId", m_activeProjectId}};
  }

  ProjectStore *store = project(params.value("projectId").toString());
  if (method.startsWith("profile.") || method == "job.submit" || method == "job.list" ||
      method.startsWith("schedule.")) {
    if (!store) return failure("not_found", "Project not found", 404);
  }
  if (method == "profile.list") return {{"profiles", store->profiles()}};
  if (method == "profile.get") {
    const CaptureProfile profile = store->profile(params.value("profileId").toString("default"));
    if (profile.id.isEmpty()) return failure("not_found", "Profile not found", 404);
    return {{"profile", toJson(profile)}};
  }
  if (method == "profile.save") {
    CaptureProfile profile = profileFromJson(params.value("profile").toObject());
    QString error;
    if (!store->saveProfile(profile, &error)) return failure("profile_save_failed", error);
    return {{"profile", toJson(profile)}};
  }
  if (method == "job.submit") {
    JobRequest request;
    request.id = newId();
    request.source = params.value("source").toString("api");
    request.profileId = params.value("profileId").toString("default");
    request.urls = strings(params.value("urls"));
    request.profile = params.value("profile").isObject()
        ? profileFromJson(params.value("profile").toObject()) : store->profile(request.profileId);
    if (request.profile.comparisonEnabled) {
      for (const auto &url : request.urls) {
        for (const auto &engine : request.profile.engines) {
          for (const auto &viewport : request.profile.viewports) {
            if (!viewport.enabled) continue;
            for (const auto &format : request.profile.formats) {
              if (format == "pdf" || (format == "pdf" && engine != "chromium")) continue;
              const QString key = comparisonKey(url, engine, viewport, request.profile, format);
              const QJsonObject baseline = store->baseline(key);
              if (!baseline.isEmpty()) request.baselines.insert(key, baseline);
            }
          }
        }
      }
    }
    QString error;
    const QString jobId = m_jobs.submit(store, request, &error);
    if (jobId.isEmpty()) return failure("job_rejected", error);
    return {{"jobId", jobId}, {"status", "queued"}, {"projectId", store->projectId()}};
  }
  if (method == "job.list") return {{"jobs", store->jobs(params.value("limit").toInt(200))}};
  if (method == "job.get" || method == "job.events" || method == "job.cancel" || method == "job.retry") {
    const QString jobId = params.value("jobId").toString();
    store = projectForJob(jobId);
    if (!store) return failure("not_found", "Job not found", 404);
    if (method == "job.get") return {{"job", store->job(jobId)}, {"comparisons", store->comparisons(jobId)}};
    if (method == "job.events") {
      return {{"events", store->events(jobId, params.value("afterSequence").toVariant().toLongLong())}};
    }
    QString error;
    if (method == "job.cancel") {
      if (!m_jobs.cancel(jobId, &error)) return failure("cancel_failed", error, 409);
      return {{"jobId", jobId}, {"status", "cancelling"}};
    }
    const QString retryId = m_jobs.retry(store, jobId, &error);
    if (retryId.isEmpty()) return failure("retry_failed", error);
    return {{"jobId", retryId}, {"status", "queued"}};
  }
  if (method == "artifact.list") {
    const QString jobId = params.value("jobId").toString();
    store = jobId.isEmpty() ? store : projectForJob(jobId);
    if (!store) return failure("not_found", "Project or job not found", 404);
    return {{"artifacts", store->artifacts(jobId)}};
  }
  if (method == "artifact.resolve") {
    const QString artifactId = params.value("artifactId").toString();
    store = projectForArtifact(artifactId);
    if (!store) return failure("not_found", "Artifact not found", 404);
    const QJsonObject artifact = store->artifact(artifactId);
    const QString root = QFileInfo(store->root()).absoluteFilePath();
    const QString absolute = QFileInfo(QDir(root).filePath(artifact.value("relativePath").toString())).absoluteFilePath();
    if (absolute != root && !absolute.startsWith(root + QDir::separator())) {
      return failure("invalid_artifact", "Artifact path escapes the project", 400);
    }
    return {{"artifact", artifact}, {"absolutePath", absolute}};
  }
  if (method == "baseline.set") {
    const QString artifactId = params.value("artifactId").toString();
    store = projectForArtifact(artifactId);
    if (!store) return failure("not_found", "Artifact not found", 404);
    const QJsonObject artifact = store->artifact(artifactId);
    if (artifact.value("status").toString() != "succeeded") {
      return failure("invalid_baseline", "Only successfully created artifacts can be used as baselines", 409);
    }
    if (artifact.value("format").toString().compare("pdf", Qt::CaseInsensitive) == 0) {
      return failure("invalid_baseline", "PDF artifacts cannot be used as visual comparison baselines", 409);
    }
    const QString key = params.value("comparisonKey").toString(comparisonKey(artifact));
    const QString root = QFileInfo(store->root()).absoluteFilePath();
    const QString source = QFileInfo(QDir(root).filePath(artifact.value("relativePath").toString())).absoluteFilePath();
    if ((source != root && !source.startsWith(root + QDir::separator())) || !QFileInfo::exists(source)) {
      return failure("baseline_failed", "The source artifact is missing or outside the project");
    }
    QString extension = artifact.value("format").toString("png").toLower();
    if (!QStringList{"png", "webp", "avif"}.contains(extension)) {
      return failure("invalid_baseline", "Unsupported visual comparison baseline format", 409);
    }
    const QString baselineName = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256)
                                                          .toHex().left(32)) + "." + extension;
    const QString destination = QDir(root).filePath("baselines/" + baselineName);
    QFile input(source);
    QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
      return failure("baseline_failed", "Could not copy the baseline artifact");
    }
    while (!input.atEnd()) {
      const QByteArray chunk = input.read(1024 * 1024);
      if (chunk.isEmpty() && input.error() != QFileDevice::NoError) {
        output.cancelWriting();
        return failure("baseline_failed", input.errorString());
      }
      if (output.write(chunk) != chunk.size()) {
        output.cancelWriting();
        return failure("baseline_failed", output.errorString());
      }
    }
    if (!output.commit()) return failure("baseline_failed", output.errorString());
    const QString relativePath = QDir(root).relativeFilePath(destination);
    if (!store->setBaseline(key, artifactId, relativePath)) return failure("baseline_failed", "Could not save baseline");
    return {{"baseline", store->baseline(key)}};
  }
  if (method == "baseline.get") {
    if (!store) return failure("not_found", "Project not found", 404);
    return {{"baseline", store->baseline(params.value("comparisonKey").toString())}};
  }
  if (method == "comparison.list") {
    store = projectForJob(params.value("jobId").toString());
    if (!store) return failure("not_found", "Job not found", 404);
    return {{"comparisons", store->comparisons(params.value("jobId").toString())}};
  }
  if (method == "schedule.list") return {{"schedules", store->schedules()}};
  if (method == "schedule.upsert") {
    QJsonObject schedule = params.value("schedule").isObject() ? params.value("schedule").toObject() : params;
    schedule.insert("id", schedule.value("id").toString(newId()));
    const QStringList urls = strings(schedule.value("urls"));
    if (urls.isEmpty()) return failure("invalid_schedule", "A schedule needs at least one URL");
    schedule.insert("urls", asJson(urls));
    QString recurrenceError;
    const QDateTime next = Scheduler::nextOccurrence(schedule.value("recurrence").toObject(),
                                                      QDateTime::currentDateTimeUtc().addSecs(-1),
                                                      &recurrenceError);
    if (!next.isValid()) return failure("invalid_recurrence", recurrenceError);
    schedule.insert("nextRun", next.toString(Qt::ISODateWithMs));
    QString error;
    if (!store->upsertSchedule(schedule, &error)) return failure("schedule_save_failed", error);
    emit eventPublished("schedule.changed", {{"projectId", store->projectId()}, {"scheduleId", schedule.value("id")}});
    return {{"schedule", schedule}};
  }
  if (method == "schedule.remove") {
    if (!store->removeSchedule(params.value("scheduleId").toString())) {
      return failure("schedule_remove_failed", "Could not remove schedule");
    }
    return {{"removed", true}};
  }
  if (method == "schedule.runNow") {
    const QString id = params.value("scheduleId").toString();
    QJsonObject selected;
    for (const auto &value : store->schedules()) if (value.toObject().value("id").toString() == id) selected = value.toObject();
    if (selected.isEmpty()) return failure("not_found", "Schedule not found", 404);
    return handle("job.submit", {{"projectId", store->projectId()}, {"profileId", selected.value("profileId")},
                                  {"urls", selected.value("urls")}, {"source", "schedule-manual:" + id}});
  }
  if (method == "api.status") {
    return {{"enabled", m_rest.isRunning()}, {"port", int(m_rest.port())}, {"hasToken", !m_settings.value("api/tokenHash").toByteArray().isEmpty()}};
  }
  if (method == "api.setEnabled") {
    QString token;
    QString error;
    if (!configureApi(params.value("enabled").toBool(), &token, &error)) return failure("api_start_failed", error, 409);
    QJsonObject result{{"enabled", m_rest.isRunning()}, {"port", int(m_rest.port())}};
    if (!token.isEmpty()) result.insert("token", token);
    return result;
  }
  if (method == "api.regenerateToken") {
    const QString token = generateToken();
    m_settings.setValue("api/tokenHash", tokenHash(token));
    const bool wasRunning = m_rest.isRunning();
    m_rest.stop();
    QString error;
    if (wasRunning && !configureApi(true, nullptr, &error)) return failure("api_start_failed", error, 409);
    return {{"token", token}, {"enabled", m_rest.isRunning()}, {"port", int(m_rest.port())}};
  }
  if (method == "settings.get") {
    return {{"maximumActiveJobs", m_settings.value("jobs/maximumActive", 1).toInt()},
            {"defaultProjectRoot", Paths::defaultProjectRoot()},
            {"workerEntry", Paths::workerEntry()},
            {"nodeExecutable", Paths::nodeExecutable()}};
  }
  if (method == "settings.set") {
    if (params.contains("maximumActiveJobs")) {
      const int maximum = qBound(1, params.value("maximumActiveJobs").toInt(), 2);
      m_settings.setValue("jobs/maximumActive", maximum);
      m_jobs.setMaximumActiveJobs(maximum);
    }
    return handle("settings.get", {});
  }
  if (method == "browser.status") {
    const QString worker = Paths::workerEntry();
    if (worker.isEmpty()) return failure("worker_missing", "Capture worker is not built", 503);
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString cache = Paths::browserCacheDir();
    environment.insert("CYBERSNAPPER_BROWSER_CACHE", cache);
    environment.insert("PLAYWRIGHT_BROWSERS_PATH", cache);
    process.setProcessEnvironment(environment);
    process.start(Paths::nodeExecutable(), {worker, "--browsers"});
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000)) {
      return failure("browser_status_failed", process.errorString(), 503);
    }
    const QJsonObject result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if (process.exitCode() != 0 || result.isEmpty()) {
      return failure("browser_status_failed", QString::fromUtf8(process.readAllStandardError()).trimmed(), 503);
    }
    return result;
  }
  if (method == "browser.install") {
    const QString engine = params.value("engine").toString();
    if (!QStringList{"chromium", "firefox", "webkit"}.contains(engine)) {
      return failure("invalid_browser", "Browser must be chromium, firefox, or webkit");
    }
    const QString worker = Paths::workerEntry();
    if (worker.isEmpty()) return failure("worker_missing", "Capture worker is not built", 503);
    auto *process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString cache = Paths::browserCacheDir();
    environment.insert("CYBERSNAPPER_BROWSER_CACHE", cache);
    environment.insert("PLAYWRIGHT_BROWSERS_PATH", cache);
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyRead, this, [this, process, engine] {
      emit eventPublished("browser.install.progress", {{"engine", engine},
                           {"message", QString::fromUtf8(process->readAll()).trimmed()}});
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, engine](int code, QProcess::ExitStatus status) {
      emit eventPublished("browser.install.finished", {{"engine", engine},
                           {"ok", status == QProcess::NormalExit && code == 0}, {"exitCode", code}});
      process->deleteLater();
    });
    process->start(Paths::nodeExecutable(), {worker, "--install", engine});
    return {{"accepted", true}, {"engine", engine}};
  }
  return failure("method_not_found", "Unknown agent method: " + method, 404);
}

} // namespace CyberSnapper
