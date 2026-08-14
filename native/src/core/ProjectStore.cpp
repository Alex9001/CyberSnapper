#include "core/ProjectStore.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace CyberSnapper {

namespace {

QString compactJson(const QJsonValue &value) {
  if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
  return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

QJsonArray parseArray(const QVariant &value) {
  const auto doc = QJsonDocument::fromJson(value.toByteArray());
  return doc.isArray() ? doc.array() : QJsonArray{};
}

QJsonObject queryJob(QSqlQuery &query) {
  return {{"id", query.value("id").toString()},
          {"projectId", query.value("project_id").toString()},
          {"status", query.value("status").toString()},
          {"source", query.value("source").toString()},
          {"createdAt", query.value("created_at").toString()},
          {"startedAt", query.value("started_at").toString()},
          {"finishedAt", query.value("finished_at").toString()},
          {"error", query.value("error").toString()},
          {"completedArtifacts", query.value("completed_artifacts").toInt()},
          {"failedArtifacts", query.value("failed_artifacts").toInt()},
          {"request", QJsonDocument::fromJson(query.value("request_json").toByteArray()).object()}};
}

} // namespace

ProjectStore::ProjectStore() = default;

ProjectStore::~ProjectStore() {
  if (m_db.isValid()) m_db.close();
  const QString connection = m_connectionName;
  m_db = {};
  if (!connection.isEmpty()) QSqlDatabase::removeDatabase(connection);
}

bool ProjectStore::open(const QString &root, const QString &requestedName, QString *error) {
  if (isOpen()) {
    if (error) *error = "Project store is already open";
    return false;
  }

  m_root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
  if (!Paths::ensureDirectory(m_root, error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/previews"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/diffs"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/logs"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/tmp"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath("captures"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath("baselines"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath("reports"), error)) {
    return false;
  }

  m_lock = std::make_unique<QLockFile>(QDir(m_root).filePath(".cybersnapper/project.lock"));
  m_lock->setStaleLockTime(30000);
  if (!m_lock->tryLock(100)) {
    if (error) *error = QStringLiteral("Project is already open in another CyberSnapper agent: %1").arg(m_root);
    return false;
  }

  const QString manifestPath = QDir(m_root).filePath("project.cybersnapper.json");
  if (QFile file(manifestPath); file.open(QIODevice::ReadOnly)) {
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    m_projectId = manifest.value("projectId").toString();
    m_projectName = manifest.value("name").toString();
  }
  if (m_projectId.isEmpty()) m_projectId = newId();
  if (m_projectName.isEmpty()) {
    m_projectName = requestedName.trimmed();
    if (m_projectName.isEmpty()) m_projectName = QFileInfo(m_root).fileName();
    if (m_projectName.isEmpty()) m_projectName = "CyberSnapper Project";
  }

  m_connectionName = "cybersnapper-project-" + m_projectId;
  m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  m_db.setDatabaseName(QDir(m_root).filePath(".cybersnapper/project.sqlite"));
  if (!m_db.open()) {
    if (error) *error = m_db.lastError().text();
    return false;
  }
  execute("PRAGMA foreign_keys = ON");
  execute("PRAGMA journal_mode = WAL");
  execute("PRAGMA synchronous = NORMAL");
  if (!migrate(error) || !writeManifest(error)) return false;

  QSqlQuery interrupted(m_db);
  interrupted.prepare("UPDATE jobs SET status='interrupted', finished_at=?, error="
                      "CASE WHEN error='' THEN 'Agent stopped before the job completed' ELSE error END "
                      "WHERE status IN ('preparing','running','cancelling')");
  interrupted.addBindValue(utcNow());
  interrupted.exec();

  CaptureProfile existing = profile("default");
  if (existing.id.isEmpty() && !saveProfile(defaultProfile(), error)) return false;
  return true;
}

bool ProjectStore::isOpen() const { return m_db.isValid() && m_db.isOpen(); }
QString ProjectStore::root() const { return m_root; }
QString ProjectStore::projectId() const { return m_projectId; }
QString ProjectStore::projectName() const { return m_projectName; }

bool ProjectStore::execute(const QString &sql, QString *error) const {
  QSqlQuery query(m_db);
  if (query.exec(sql)) return true;
  if (error) *error = query.lastError().text() + " — " + sql;
  return false;
}

bool ProjectStore::migrate(QString *error) {
  const QStringList statements = {
      "CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
      "CREATE TABLE IF NOT EXISTS profiles (id TEXT PRIMARY KEY, name TEXT NOT NULL, json TEXT NOT NULL, updated_at TEXT NOT NULL)",
      "CREATE TABLE IF NOT EXISTS jobs (id TEXT PRIMARY KEY, project_id TEXT NOT NULL, status TEXT NOT NULL, source TEXT NOT NULL, created_at TEXT NOT NULL, started_at TEXT DEFAULT '', finished_at TEXT DEFAULT '', error TEXT DEFAULT '', completed_artifacts INTEGER DEFAULT 0, failed_artifacts INTEGER DEFAULT 0, request_json TEXT NOT NULL)",
      "CREATE INDEX IF NOT EXISTS jobs_created_idx ON jobs(created_at DESC)",
      "CREATE TABLE IF NOT EXISTS events (job_id TEXT NOT NULL, sequence INTEGER NOT NULL, type TEXT NOT NULL, created_at TEXT NOT NULL, event_json TEXT NOT NULL, PRIMARY KEY(job_id, sequence), FOREIGN KEY(job_id) REFERENCES jobs(id) ON DELETE CASCADE)",
      "CREATE TABLE IF NOT EXISTS artifacts (id TEXT PRIMARY KEY, job_id TEXT NOT NULL, target_url TEXT NOT NULL, engine TEXT NOT NULL, viewport_id TEXT NOT NULL, viewport_name TEXT NOT NULL, capture_mode TEXT NOT NULL, format TEXT NOT NULL, relative_path TEXT NOT NULL, width INTEGER DEFAULT 0, height INTEGER DEFAULT 0, sha256 TEXT DEFAULT '', status TEXT NOT NULL, error TEXT DEFAULT '', created_at TEXT NOT NULL, metadata_json TEXT NOT NULL, FOREIGN KEY(job_id) REFERENCES jobs(id) ON DELETE CASCADE)",
      "CREATE INDEX IF NOT EXISTS artifacts_job_idx ON artifacts(job_id)",
      "CREATE TABLE IF NOT EXISTS baselines (comparison_key TEXT PRIMARY KEY, artifact_id TEXT NOT NULL, relative_path TEXT DEFAULT '', updated_at TEXT NOT NULL)",
      "CREATE TABLE IF NOT EXISTS comparisons (id TEXT PRIMARY KEY, job_id TEXT NOT NULL, comparison_key TEXT NOT NULL, baseline_artifact_id TEXT, current_artifact_id TEXT, status TEXT NOT NULL, mismatch_ratio REAL DEFAULT 0, diff_relative_path TEXT DEFAULT '', created_at TEXT NOT NULL, metadata_json TEXT NOT NULL)",
      "CREATE INDEX IF NOT EXISTS comparisons_job_idx ON comparisons(job_id)",
      "CREATE TABLE IF NOT EXISTS schedules (id TEXT PRIMARY KEY, name TEXT NOT NULL, enabled INTEGER NOT NULL, profile_id TEXT NOT NULL, urls_json TEXT NOT NULL, recurrence_json TEXT NOT NULL, last_run TEXT DEFAULT '', next_run TEXT NOT NULL, last_status TEXT DEFAULT '', updated_at TEXT NOT NULL)",
  };
  if (!m_db.transaction()) {
    if (error) *error = m_db.lastError().text();
    return false;
  }
  for (const auto &statement : statements) {
    if (!execute(statement, error)) {
      m_db.rollback();
      return false;
    }
  }
  bool baselinePathColumn = false;
  QSqlQuery columns("PRAGMA table_info(baselines)", m_db);
  while (columns.next()) {
    if (columns.value("name").toString() == "relative_path") baselinePathColumn = true;
  }
  columns.finish();
  if (!baselinePathColumn && !execute("ALTER TABLE baselines ADD COLUMN relative_path TEXT DEFAULT ''", error)) {
    m_db.rollback();
    return false;
  }
  QSqlQuery version(m_db);
  version.prepare("INSERT INTO metadata(key,value) VALUES('schemaVersion','2') ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  if (!version.exec() || !m_db.commit()) {
    if (error) *error = version.lastError().text().isEmpty() ? m_db.lastError().text() : version.lastError().text();
    return false;
  }
  return true;
}

bool ProjectStore::writeManifest(QString *error) {
  QString createdAt = utcNow();
  if (QFile existing(QDir(m_root).filePath("project.cybersnapper.json"));
      existing.open(QIODevice::ReadOnly)) {
    const QString preserved = QJsonDocument::fromJson(existing.readAll()).object().value("createdAt").toString();
    if (!preserved.isEmpty()) createdAt = preserved;
  }
  QSaveFile file(QDir(m_root).filePath("project.cybersnapper.json"));
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = file.errorString();
    return false;
  }
  const QJsonObject manifest{{"schemaVersion", 1},
                             {"projectId", m_projectId},
                             {"name", m_projectName},
                             {"createdAt", createdAt},
                             {"database", ".cybersnapper/project.sqlite"},
                             {"captureRoot", "captures"}};
  file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
  if (file.commit()) return true;
  if (error) *error = file.errorString();
  return false;
}

CaptureProfile ProjectStore::profile(const QString &id) const {
  QSqlQuery query(m_db);
  query.prepare("SELECT json FROM profiles WHERE id=?");
  query.addBindValue(id);
  if (!query.exec() || !query.next()) return CaptureProfile{};
  return profileFromJson(QJsonDocument::fromJson(query.value(0).toByteArray()).object());
}

QJsonArray ProjectStore::profiles() const {
  QJsonArray out;
  QSqlQuery query("SELECT json FROM profiles ORDER BY name COLLATE NOCASE", m_db);
  while (query.next()) out.append(QJsonDocument::fromJson(query.value(0).toByteArray()).object());
  return out;
}

bool ProjectStore::saveProfile(const CaptureProfile &profileValue, QString *error) {
  CaptureProfile normalized = profileFromJson(toJson(profileValue));
  if (normalized.id.isEmpty()) normalized.id = newId();
  QSqlQuery query(m_db);
  query.prepare("INSERT INTO profiles(id,name,json,updated_at) VALUES(?,?,?,?) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name,json=excluded.json,updated_at=excluded.updated_at");
  query.addBindValue(normalized.id);
  query.addBindValue(normalized.name);
  query.addBindValue(compactJson(toJson(normalized)));
  query.addBindValue(utcNow());
  if (query.exec()) return true;
  if (error) *error = query.lastError().text();
  return false;
}

bool ProjectStore::insertJob(const JobRequest &request, QString *error) {
  QSqlQuery query(m_db);
  query.prepare("INSERT INTO jobs(id,project_id,status,source,created_at,request_json) VALUES(?,?,?,?,?,?)");
  query.addBindValue(request.id);
  query.addBindValue(m_projectId);
  query.addBindValue("queued");
  query.addBindValue(request.source);
  query.addBindValue(utcNow());
  query.addBindValue(compactJson(toJson(request)));
  if (query.exec()) return true;
  if (error) *error = query.lastError().text();
  return false;
}

bool ProjectStore::updateJob(const QString &jobId, const QString &status, const QString &error,
                             int completedDelta, int failedDelta) {
  const QString normalized = normalizedJobStatus(status);
  QSqlQuery query(m_db);
  query.prepare("UPDATE jobs SET status=?, error=CASE WHEN ?='' THEN error ELSE ? END, "
                "started_at=CASE WHEN ?='running' AND started_at='' THEN ? ELSE started_at END, "
                "finished_at=CASE WHEN ? IN ('succeeded','partial','failed','cancelled','interrupted') THEN ? ELSE finished_at END, "
                "completed_artifacts=completed_artifacts+?, failed_artifacts=failed_artifacts+? WHERE id=?");
  query.addBindValue(normalized);
  query.addBindValue(error);
  query.addBindValue(error);
  query.addBindValue(normalized);
  query.addBindValue(utcNow());
  query.addBindValue(normalized);
  query.addBindValue(utcNow());
  query.addBindValue(completedDelta);
  query.addBindValue(failedDelta);
  query.addBindValue(jobId);
  return query.exec();
}

QJsonObject ProjectStore::job(const QString &jobId) const {
  QSqlQuery query(m_db);
  query.prepare("SELECT * FROM jobs WHERE id=?");
  query.addBindValue(jobId);
  if (!query.exec() || !query.next()) return {};
  QJsonObject out = queryJob(query);
  out.insert("artifacts", artifacts(jobId));
  return out;
}

QJsonArray ProjectStore::jobs(int limit) const {
  QJsonArray out;
  QSqlQuery query(m_db);
  query.prepare("SELECT * FROM jobs ORDER BY created_at DESC LIMIT ?");
  query.addBindValue(qBound(1, limit, 5000));
  if (!query.exec()) return out;
  while (query.next()) out.append(queryJob(query));
  return out;
}

bool ProjectStore::appendEvent(const QString &jobId, const QJsonObject &event) {
  QSqlQuery query(m_db);
  query.prepare("INSERT OR REPLACE INTO events(job_id,sequence,type,created_at,event_json) VALUES(?,?,?,?,?)");
  query.addBindValue(jobId);
  query.addBindValue(event.value("sequence").toVariant().toLongLong());
  query.addBindValue(event.value("type").toString());
  query.addBindValue(event.value("timestamp").toString(utcNow()));
  query.addBindValue(compactJson(event));
  return query.exec();
}

QJsonArray ProjectStore::events(const QString &jobId, qint64 afterSequence) const {
  QJsonArray out;
  QSqlQuery query(m_db);
  query.prepare("SELECT event_json FROM events WHERE job_id=? AND sequence>? ORDER BY sequence ASC");
  query.addBindValue(jobId);
  query.addBindValue(afterSequence);
  if (!query.exec()) return out;
  while (query.next()) out.append(QJsonDocument::fromJson(query.value(0).toByteArray()).object());
  return out;
}

bool ProjectStore::insertArtifact(const QString &jobId, const QJsonObject &artifactValue) {
  QJsonObject artifact = artifactValue;
  const QString id = artifact.value("id").toString(newId());
  QSqlQuery query(m_db);
  query.prepare("INSERT OR REPLACE INTO artifacts(id,job_id,target_url,engine,viewport_id,viewport_name,capture_mode,format,relative_path,width,height,sha256,status,error,created_at,metadata_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  query.addBindValue(id);
  query.addBindValue(jobId);
  query.addBindValue(artifact.value("url").toString());
  query.addBindValue(artifact.value("engine").toString());
  query.addBindValue(artifact.value("viewportId").toString());
  query.addBindValue(artifact.value("viewportName").toString());
  query.addBindValue(artifact.value("captureMode").toString());
  query.addBindValue(artifact.value("format").toString());
  query.addBindValue(artifact.value("relativePath").toString());
  query.addBindValue(artifact.value("width").toInt());
  query.addBindValue(artifact.value("height").toInt());
  query.addBindValue(artifact.value("sha256").toString());
  query.addBindValue(artifact.value("status").toString("succeeded"));
  query.addBindValue(artifact.value("error").toString());
  query.addBindValue(artifact.value("createdAt").toString(utcNow()));
  artifact.insert("id", id);
  query.addBindValue(compactJson(artifact));
  return query.exec();
}

QJsonArray ProjectStore::artifacts(const QString &jobId) const {
  QJsonArray out;
  QSqlQuery query(m_db);
  if (jobId.isEmpty()) {
    query.prepare("SELECT metadata_json FROM artifacts ORDER BY created_at DESC LIMIT 2000");
  } else {
    query.prepare("SELECT metadata_json FROM artifacts WHERE job_id=? ORDER BY created_at ASC");
    query.addBindValue(jobId);
  }
  if (!query.exec()) return out;
  while (query.next()) out.append(QJsonDocument::fromJson(query.value(0).toByteArray()).object());
  return out;
}

QJsonObject ProjectStore::artifact(const QString &artifactId) const {
  QSqlQuery query(m_db);
  query.prepare("SELECT metadata_json FROM artifacts WHERE id=?");
  query.addBindValue(artifactId);
  if (!query.exec() || !query.next()) return {};
  return QJsonDocument::fromJson(query.value(0).toByteArray()).object();
}

bool ProjectStore::upsertSchedule(const QJsonObject &schedule, QString *error) {
  const QString id = schedule.value("id").toString(newId());
  QSqlQuery query(m_db);
  query.prepare("INSERT INTO schedules(id,name,enabled,profile_id,urls_json,recurrence_json,last_run,next_run,last_status,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name,enabled=excluded.enabled,profile_id=excluded.profile_id,urls_json=excluded.urls_json,recurrence_json=excluded.recurrence_json,last_run=excluded.last_run,next_run=excluded.next_run,last_status=excluded.last_status,updated_at=excluded.updated_at");
  query.addBindValue(id);
  query.addBindValue(schedule.value("name").toString("Schedule"));
  query.addBindValue(schedule.value("enabled").toBool(true) ? 1 : 0);
  query.addBindValue(schedule.value("profileId").toString("default"));
  query.addBindValue(compactJson(schedule.value("urls")));
  query.addBindValue(compactJson(schedule.value("recurrence")));
  query.addBindValue(schedule.value("lastRun").toString());
  query.addBindValue(schedule.value("nextRun").toString());
  query.addBindValue(schedule.value("lastStatus").toString());
  query.addBindValue(utcNow());
  if (query.exec()) return true;
  if (error) *error = query.lastError().text();
  return false;
}

bool ProjectStore::removeSchedule(const QString &scheduleId) {
  QSqlQuery query(m_db);
  query.prepare("DELETE FROM schedules WHERE id=?");
  query.addBindValue(scheduleId);
  return query.exec();
}

QJsonArray ProjectStore::schedules(bool enabledOnly) const {
  QJsonArray out;
  QString sql = "SELECT * FROM schedules";
  if (enabledOnly) sql += " WHERE enabled=1";
  sql += " ORDER BY next_run ASC";
  QSqlQuery query(sql, m_db);
  while (query.next()) {
    out.append(QJsonObject{{"id", query.value("id").toString()},
                           {"name", query.value("name").toString()},
                           {"enabled", query.value("enabled").toBool()},
                           {"profileId", query.value("profile_id").toString()},
                           {"urls", parseArray(query.value("urls_json"))},
                           {"recurrence", parseObject(query.value("recurrence_json"))},
                           {"lastRun", query.value("last_run").toString()},
                           {"nextRun", query.value("next_run").toString()},
                           {"lastStatus", query.value("last_status").toString()}});
  }
  return out;
}

bool ProjectStore::updateScheduleRun(const QString &scheduleId, const QString &lastRun,
                                     const QString &nextRun, const QString &lastStatus) {
  QSqlQuery query(m_db);
  query.prepare("UPDATE schedules SET last_run=?,next_run=?,last_status=?,updated_at=? WHERE id=?");
  query.addBindValue(lastRun);
  query.addBindValue(nextRun);
  query.addBindValue(lastStatus);
  query.addBindValue(utcNow());
  query.addBindValue(scheduleId);
  return query.exec();
}

bool ProjectStore::setBaseline(const QString &key, const QString &artifactId, const QString &relativePath) {
  QSqlQuery query(m_db);
  query.prepare("INSERT INTO baselines(comparison_key,artifact_id,relative_path,updated_at) VALUES(?,?,?,?) "
                "ON CONFLICT(comparison_key) DO UPDATE SET artifact_id=excluded.artifact_id,relative_path=excluded.relative_path,updated_at=excluded.updated_at");
  query.addBindValue(key);
  query.addBindValue(artifactId);
  query.addBindValue(relativePath);
  query.addBindValue(utcNow());
  return query.exec();
}

QJsonObject ProjectStore::baseline(const QString &key) const {
  QSqlQuery query(m_db);
  query.prepare("SELECT b.comparison_key,b.artifact_id,b.relative_path,b.updated_at,a.metadata_json FROM baselines b LEFT JOIN artifacts a ON a.id=b.artifact_id WHERE b.comparison_key=?");
  query.addBindValue(key);
  if (!query.exec() || !query.next()) return {};
  QJsonObject artifactValue = parseObject(query.value(4));
  const QString relativePath = query.value(2).toString();
  if (!relativePath.isEmpty()) artifactValue.insert("relativePath", relativePath);
  return {{"comparisonKey", query.value(0).toString()},
          {"artifactId", query.value(1).toString()},
          {"relativePath", relativePath},
          {"updatedAt", query.value(3).toString()},
          {"artifact", artifactValue}};
}

bool ProjectStore::insertComparison(const QJsonObject &comparison) {
  QSqlQuery query(m_db);
  query.prepare("INSERT OR REPLACE INTO comparisons(id,job_id,comparison_key,baseline_artifact_id,current_artifact_id,status,mismatch_ratio,diff_relative_path,created_at,metadata_json) VALUES(?,?,?,?,?,?,?,?,?,?)");
  query.addBindValue(comparison.value("id").toString(newId()));
  query.addBindValue(comparison.value("jobId").toString());
  query.addBindValue(comparison.value("comparisonKey").toString());
  query.addBindValue(comparison.value("baselineArtifactId").toString());
  query.addBindValue(comparison.value("currentArtifactId").toString());
  query.addBindValue(comparison.value("status").toString());
  query.addBindValue(comparison.value("mismatchRatio").toDouble());
  query.addBindValue(comparison.value("diffRelativePath").toString());
  query.addBindValue(comparison.value("createdAt").toString(utcNow()));
  query.addBindValue(compactJson(comparison));
  return query.exec();
}

QJsonArray ProjectStore::comparisons(const QString &jobId) const {
  QJsonArray out;
  QSqlQuery query(m_db);
  query.prepare("SELECT metadata_json FROM comparisons WHERE job_id=? ORDER BY created_at ASC");
  query.addBindValue(jobId);
  if (!query.exec()) return out;
  while (query.next()) out.append(QJsonDocument::fromJson(query.value(0).toByteArray()).object());
  return out;
}

QJsonObject ProjectStore::parseObject(const QVariant &value) {
  return QJsonDocument::fromJson(value.toByteArray()).object();
}

} // namespace CyberSnapper
