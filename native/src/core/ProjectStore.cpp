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

bool hasColumn(QSqlDatabase database, const QString &table, const QString &column) {
  QSqlQuery query(database);
  if (!query.exec("PRAGMA table_info(" + table + ")")) return false;
  while (query.next()) if (query.value("name").toString() == column) return true;
  return false;
}

QJsonObject queryComparison(QSqlQuery &query) {
  QJsonObject out = QJsonDocument::fromJson(query.value("metadata_json").toByteArray()).object();
  out.insert("id", query.value("id").toString());
  out.insert("jobId", query.value("job_id").toString());
  out.insert("comparisonKey", query.value("comparison_key").toString());
  out.insert("baselineArtifactId", query.value("baseline_artifact_id").toString());
  out.insert("currentArtifactId", query.value("current_artifact_id").toString());
  out.insert("status", query.value("status").toString());
  out.insert("mismatchRatio", query.value("mismatch_ratio").toDouble());
  out.insert("diffRelativePath", query.value("diff_relative_path").toString());
  out.insert("createdAt", query.value("created_at").toString());
  out.insert("url", query.value("target_url").toString());
  out.insert("targetId", query.value("target_id").toString());
  out.insert("targetName", query.value("target_name").toString());
  out.insert("targetSetId", query.value("target_set_id").toString());
  out.insert("targetSetName", query.value("target_set_name").toString());
  out.insert("engine", query.value("engine").toString());
  out.insert("viewportId", query.value("viewport_id").toString());
  out.insert("viewportName", query.value("viewport_name").toString());
  out.insert("captureMode", query.value("capture_mode").toString());
  out.insert("format", query.value("format").toString());
  out.insert("analysisWidth", query.value("analysis_width").toInt());
  out.insert("analysisHeight", query.value("analysis_height").toInt());
  out.insert("analysisScale", query.value("analysis_scale").toDouble());
  out.insert("baselineRelativePath", query.value("baseline_relative_path").toString());
  if (out.value("status").toString() == "matched") {
    out.insert("review", QJsonValue::Null);
  } else {
    const bool reviewed = !query.value("review_status").isNull();
    out.insert("review", QJsonObject{{"status", reviewed ? query.value("review_status").toString() : "unreviewed"},
                                     {"note", reviewed ? query.value("review_note").toString() : QString{}},
                                     {"reviewedAt", reviewed ? query.value("reviewed_at").toString() : QString{}},
                                     {"updatedAt", reviewed ? query.value("review_updated_at").toString() : QString{}},
                                     {"revision", reviewed ? query.value("review_revision").toInt() : 0},
                                     {"promotedArtifactId", reviewed ? query.value("promoted_artifact_id").toString() : QString{}}});
  }
  return out;
}

} // namespace

ProjectStore::ProjectStore() = default;

ProjectStore::~ProjectStore() {
  if (m_db.isValid()) m_db.close();
  const QString connection = m_connectionName;
  m_db = {};
  if (!connection.isEmpty()) QSqlDatabase::removeDatabase(connection);
}

bool ProjectStore::open(const QString &root, QString *error) {
  return openInternal(root, {}, false, error);
}

bool ProjectStore::create(const QString &root, const QString &requestedName, QString *error) {
  return openInternal(root, requestedName, true, error);
}

bool ProjectStore::openInternal(const QString &root, const QString &requestedName,
                                bool createProject, QString *error) {
  if (isOpen()) {
    if (error) *error = "Project store is already open";
    return false;
  }

  m_root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
  const QString manifestPath = QDir(m_root).filePath("project.cybersnapper.json");
  QJsonObject manifest;
  if (createProject) {
    QDir destination(m_root);
    if (destination.exists() && !destination.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
      if (error) *error = "A new project folder must be empty: " + m_root;
      return false;
    }
  } else {
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
      if (error) *error = "This folder is not a CyberSnapper 2 project (project.cybersnapper.json is missing)";
      return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    manifest = document.object();
    if (!document.isObject() || manifest.value("projectId").toString().isEmpty() ||
        manifest.value("schemaVersion").toInt() != 1) {
      if (error) *error = parseError.error == QJsonParseError::NoError
          ? "This project manifest is invalid or unsupported"
          : "Could not parse the project manifest: " + parseError.errorString();
      return false;
    }
    const QString databaseRelative = manifest.value("database").toString();
    const QFileInfo databaseInfo(QDir(m_root).filePath(databaseRelative));
    const QString databasePath = databaseInfo.canonicalFilePath();
    const QString canonicalRoot = QFileInfo(m_root).canonicalFilePath();
    const QString databaseFromRoot = canonicalRoot.isEmpty() || databasePath.isEmpty()
        ? QString{} : QDir(canonicalRoot).relativeFilePath(databasePath);
    if (databaseRelative.isEmpty() ||
        canonicalRoot.isEmpty() || databasePath.isEmpty() ||
        databaseFromRoot == ".." || databaseFromRoot.startsWith("../") ||
        QDir::isAbsolutePath(databaseFromRoot) ||
        !databaseInfo.isFile()) {
      if (error) *error = "The project database is missing or outside the project folder";
      return false;
    }
  }

  if (!Paths::ensureDirectory(m_root, error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/previews"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/diffs"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/logs"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath(".cybersnapper/tmp"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath("captures"), error) ||
      !Paths::ensureDirectory(QDir(m_root).filePath("baselines"), error)) {
    return false;
  }

  m_lock = std::make_unique<QLockFile>(QDir(m_root).filePath(".cybersnapper/project.lock"));
  m_lock->setStaleLockTime(30000);
  if (!m_lock->tryLock(100)) {
    if (error) *error = QStringLiteral("Project is already open in another CyberSnapper agent: %1").arg(m_root);
    return false;
  }

  if (!manifest.isEmpty()) {
    m_projectId = manifest.value("projectId").toString();
    m_projectName = manifest.value("name").toString();
    m_allowLocalhost = manifest.value("allowLocalhost").toBool(false);
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

  QStringList interruptedIds;
  QSqlQuery interrupted("SELECT id FROM jobs WHERE status IN ('preparing','running','cancelling')", m_db);
  while (interrupted.next()) interruptedIds.append(interrupted.value(0).toString());
  if (!interruptedIds.isEmpty()) {
    if (!m_db.transaction()) { if (error) *error = m_db.lastError().text(); return false; }
    bool recovered = true;
    for (const QString &jobId : interruptedIds) {
      QSqlQuery sequenceQuery(m_db);
      sequenceQuery.prepare("SELECT COALESCE(MAX(sequence),0)+1 FROM events WHERE job_id=?");
      sequenceQuery.addBindValue(jobId);
      recovered = sequenceQuery.exec() && sequenceQuery.next();
      if (!recovered) break;
      const QJsonObject event{{"protocolVersion", 2}, {"sequence", sequenceQuery.value(0).toLongLong()},
                              {"timestamp", utcNow()}, {"type", "job_interrupted"}, {"jobId", jobId},
                              {"message", "Agent stopped before the job completed"}};
      recovered = updateJob(jobId, "interrupted", event.value("message").toString()) && appendEvent(jobId, event);
      if (!recovered) break;
    }
    if (!recovered || !m_db.commit()) {
      if (error) *error = m_db.lastError().text();
      m_db.rollback();
      return false;
    }
  }

  CaptureProfile existing = profile("default");
  if (existing.id.isEmpty() && !saveProfile(defaultProfile(), error)) return false;
  return true;
}

bool ProjectStore::isOpen() const { return m_db.isValid() && m_db.isOpen(); }
QString ProjectStore::root() const { return m_root; }
QString ProjectStore::projectId() const { return m_projectId; }
QString ProjectStore::projectName() const { return m_projectName; }
bool ProjectStore::allowLocalhost() const { return m_allowLocalhost; }

bool ProjectStore::setAllowLocalhost(bool allowed, QString *error) {
  if (m_allowLocalhost == allowed) return true;
  m_allowLocalhost = allowed;
  if (writeManifest(error)) return true;
  m_allowLocalhost = !allowed;
  return false;
}

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
      "CREATE TABLE IF NOT EXISTS artifacts (id TEXT PRIMARY KEY, job_id TEXT NOT NULL, target_url TEXT NOT NULL, target_id TEXT DEFAULT '', target_name TEXT DEFAULT '', target_set_id TEXT DEFAULT '', target_set_name TEXT DEFAULT '', engine TEXT NOT NULL, viewport_id TEXT NOT NULL, viewport_name TEXT NOT NULL, capture_mode TEXT NOT NULL, format TEXT NOT NULL, relative_path TEXT NOT NULL, width INTEGER DEFAULT 0, height INTEGER DEFAULT 0, sha256 TEXT DEFAULT '', status TEXT NOT NULL, error TEXT DEFAULT '', created_at TEXT NOT NULL, metadata_json TEXT NOT NULL, FOREIGN KEY(job_id) REFERENCES jobs(id) ON DELETE CASCADE)",
      "CREATE INDEX IF NOT EXISTS artifacts_job_idx ON artifacts(job_id)",
      "CREATE TABLE IF NOT EXISTS baselines (comparison_key TEXT PRIMARY KEY, artifact_id TEXT NOT NULL, relative_path TEXT DEFAULT '', updated_at TEXT NOT NULL)",
      "CREATE TABLE IF NOT EXISTS comparisons (id TEXT PRIMARY KEY, job_id TEXT NOT NULL, comparison_key TEXT NOT NULL, baseline_artifact_id TEXT, current_artifact_id TEXT, status TEXT NOT NULL, mismatch_ratio REAL DEFAULT 0, diff_relative_path TEXT DEFAULT '', created_at TEXT NOT NULL, target_url TEXT DEFAULT '', target_id TEXT DEFAULT '', target_name TEXT DEFAULT '', target_set_id TEXT DEFAULT '', target_set_name TEXT DEFAULT '', engine TEXT DEFAULT '', viewport_id TEXT DEFAULT '', viewport_name TEXT DEFAULT '', capture_mode TEXT DEFAULT '', format TEXT DEFAULT '', analysis_width INTEGER DEFAULT 0, analysis_height INTEGER DEFAULT 0, analysis_scale REAL DEFAULT 1, baseline_relative_path TEXT DEFAULT '', metadata_json TEXT NOT NULL)",
      "CREATE INDEX IF NOT EXISTS comparisons_job_idx ON comparisons(job_id)",
      "CREATE TABLE IF NOT EXISTS schedules (id TEXT PRIMARY KEY, name TEXT NOT NULL, enabled INTEGER NOT NULL, profile_id TEXT NOT NULL, target_set_id TEXT DEFAULT '', urls_json TEXT NOT NULL, recurrence_json TEXT NOT NULL, last_run TEXT DEFAULT '', next_run TEXT NOT NULL, last_status TEXT DEFAULT '', updated_at TEXT NOT NULL)",
      "CREATE TABLE IF NOT EXISTS target_sets (id TEXT PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE, description TEXT NOT NULL DEFAULT '', revision INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
      "CREATE TABLE IF NOT EXISTS targets (id TEXT PRIMARY KEY, target_set_id TEXT NOT NULL, position INTEGER NOT NULL, label TEXT NOT NULL DEFAULT '', url TEXT NOT NULL, enabled INTEGER NOT NULL DEFAULT 1, FOREIGN KEY(target_set_id) REFERENCES target_sets(id) ON DELETE CASCADE, UNIQUE(target_set_id, position))",
      "CREATE INDEX IF NOT EXISTS targets_set_position_idx ON targets(target_set_id,position)",
      "CREATE TABLE IF NOT EXISTS comparison_reviews (comparison_id TEXT PRIMARY KEY, status TEXT NOT NULL CHECK(status IN ('unreviewed','accepted','ignored')), note TEXT NOT NULL DEFAULT '', reviewed_at TEXT DEFAULT '', updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1, promoted_artifact_id TEXT DEFAULT '', FOREIGN KEY(comparison_id) REFERENCES comparisons(id) ON DELETE CASCADE)",
  };
  if (!m_db.transaction()) {
    if (error) *error = m_db.lastError().text();
    return false;
  }
  if (!execute(statements.first(), error)) { m_db.rollback(); return false; }
  QSqlQuery existingVersion(m_db);
  int schemaVersion = 0;
  if (existingVersion.exec("SELECT value FROM metadata WHERE key='schemaVersion'") && existingVersion.next()) {
    schemaVersion = existingVersion.value(0).toInt();
  }
  if (schemaVersion > 4) {
    if (error) *error = QStringLiteral("This project uses newer database schema %1").arg(schemaVersion);
    m_db.rollback();
    return false;
  }
  for (const auto &statement : statements) {
    if (!execute(statement, error)) {
      m_db.rollback();
      return false;
    }
  }
  const QList<QPair<QString, QString>> additions{
      {"baselines", "relative_path TEXT DEFAULT ''"},
      {"artifacts", "target_id TEXT DEFAULT ''"}, {"artifacts", "target_name TEXT DEFAULT ''"},
      {"artifacts", "target_set_id TEXT DEFAULT ''"}, {"artifacts", "target_set_name TEXT DEFAULT ''"},
      {"comparisons", "target_url TEXT DEFAULT ''"}, {"comparisons", "target_id TEXT DEFAULT ''"},
      {"comparisons", "target_name TEXT DEFAULT ''"}, {"comparisons", "target_set_id TEXT DEFAULT ''"},
      {"comparisons", "target_set_name TEXT DEFAULT ''"}, {"comparisons", "engine TEXT DEFAULT ''"},
      {"comparisons", "viewport_id TEXT DEFAULT ''"}, {"comparisons", "viewport_name TEXT DEFAULT ''"},
      {"comparisons", "capture_mode TEXT DEFAULT ''"}, {"comparisons", "format TEXT DEFAULT ''"},
      {"comparisons", "analysis_width INTEGER DEFAULT 0"}, {"comparisons", "analysis_height INTEGER DEFAULT 0"},
      {"comparisons", "analysis_scale REAL DEFAULT 1"}, {"comparisons", "baseline_relative_path TEXT DEFAULT ''"},
      {"schedules", "target_set_id TEXT DEFAULT ''"},
  };
  for (const auto &[table, definition] : additions) {
    const QString column = definition.section(' ', 0, 0);
    if (!hasColumn(m_db, table, column) && !execute("ALTER TABLE " + table + " ADD COLUMN " + definition, error)) {
      m_db.rollback(); return false;
    }
  }
  const QStringList indexes{
      "CREATE INDEX IF NOT EXISTS comparisons_status_created_idx ON comparisons(status,created_at DESC,id DESC)",
      "CREATE INDEX IF NOT EXISTS comparisons_target_created_idx ON comparisons(target_set_id,created_at DESC,id DESC)",
      "CREATE INDEX IF NOT EXISTS comparison_reviews_status_idx ON comparison_reviews(status,updated_at DESC)",
      "CREATE INDEX IF NOT EXISTS jobs_status_created_idx ON jobs(status,created_at DESC)",
      "CREATE INDEX IF NOT EXISTS schedules_target_set_idx ON schedules(target_set_id)"};
  for (const auto &statement : indexes) if (!execute(statement, error)) { m_db.rollback(); return false; }
  if (schemaVersion < 4) {
    if (!execute("UPDATE artifacts SET target_id=COALESCE(json_extract(metadata_json,'$.targetId'),''),target_name=COALESCE(json_extract(metadata_json,'$.targetName'),''),target_set_id=COALESCE(json_extract(metadata_json,'$.targetSetId'),''),target_set_name=COALESCE(json_extract(metadata_json,'$.targetSetName'),'')", error) ||
        !execute("UPDATE comparisons SET target_url=COALESCE((SELECT target_url FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),target_id=COALESCE((SELECT target_id FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),target_name=COALESCE((SELECT target_name FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),target_set_id=COALESCE((SELECT target_set_id FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),target_set_name=COALESCE((SELECT target_set_name FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),engine=COALESCE((SELECT engine FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),viewport_id=COALESCE((SELECT viewport_id FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),viewport_name=COALESCE((SELECT viewport_name FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),capture_mode=COALESCE((SELECT capture_mode FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),''),format=COALESCE((SELECT format FROM artifacts WHERE artifacts.id=comparisons.current_artifact_id),'')", error)) {
      m_db.rollback(); return false;
    }
  }
  QSqlQuery version(m_db);
  version.prepare("INSERT INTO metadata(key,value) VALUES('schemaVersion','4') ON CONFLICT(key) DO UPDATE SET value=excluded.value");
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
                             {"captureRoot", "captures"},
                             {"allowLocalhost", m_allowLocalhost}};
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

bool ProjectStore::removeProfile(const QString &id, QString *error) {
  if (id == "default") {
    if (error) *error = "The default profile cannot be removed";
    return false;
  }
  QSqlQuery query(m_db);
  query.prepare("DELETE FROM profiles WHERE id=?");
  query.addBindValue(id);
  if (query.exec() && query.numRowsAffected() > 0) return true;
  if (error) *error = query.lastError().text().isEmpty() ? "Profile not found" : query.lastError().text();
  return false;
}

QJsonArray ProjectStore::targetSets() const {
  QJsonArray out;
  QSqlQuery query(m_db);
  query.prepare("SELECT s.*,COUNT(t.id) AS target_count,SUM(CASE WHEN t.enabled=1 THEN 1 ELSE 0 END) AS enabled_count "
                "FROM target_sets s LEFT JOIN targets t ON t.target_set_id=s.id "
                "GROUP BY s.id ORDER BY s.name COLLATE NOCASE");
  if (!query.exec()) return out;
  while (query.next()) {
    out.append(QJsonObject{{"id", query.value("id").toString()}, {"name", query.value("name").toString()},
                           {"description", query.value("description").toString()},
                           {"revision", query.value("revision").toInt()},
                           {"targetCount", query.value("target_count").toInt()},
                           {"enabledCount", query.value("enabled_count").toInt()},
                           {"createdAt", query.value("created_at").toString()},
                           {"updatedAt", query.value("updated_at").toString()}});
  }
  return out;
}

QJsonObject ProjectStore::targetSet(const QString &id) const {
  QSqlQuery query(m_db);
  query.prepare("SELECT * FROM target_sets WHERE id=?");
  query.addBindValue(id);
  if (!query.exec() || !query.next()) return {};
  QJsonObject out{{"id", query.value("id").toString()}, {"name", query.value("name").toString()},
                  {"description", query.value("description").toString()},
                  {"revision", query.value("revision").toInt()},
                  {"createdAt", query.value("created_at").toString()},
                  {"updatedAt", query.value("updated_at").toString()}};
  QJsonArray targetsValue;
  QSqlQuery targetsQuery(m_db);
  targetsQuery.prepare("SELECT * FROM targets WHERE target_set_id=? ORDER BY position,id");
  targetsQuery.addBindValue(id);
  if (targetsQuery.exec()) while (targetsQuery.next()) {
    targetsValue.append(QJsonObject{{"id", targetsQuery.value("id").toString()},
                                    {"label", targetsQuery.value("label").toString()},
                                    {"url", targetsQuery.value("url").toString()},
                                    {"enabled", targetsQuery.value("enabled").toBool()},
                                    {"position", targetsQuery.value("position").toInt()}});
  }
  out.insert("targets", targetsValue);
  return out;
}

QJsonObject ProjectStore::saveTargetSet(const QJsonObject &value, QString *error) {
  const QString id = value.value("id").toString().isEmpty() ? newId() : value.value("id").toString();
  const QString now = utcNow();
  QString description = value.value("description").toString().trimmed();
  if (description.isNull()) description = QStringLiteral("");
  QSqlQuery current(m_db);
  current.prepare("SELECT revision,created_at FROM target_sets WHERE id=?");
  current.addBindValue(id);
  const bool exists = current.exec() && current.next();
  const int revision = exists ? current.value(0).toInt() + 1 : 1;
  const QString createdAt = exists ? current.value(1).toString() : now;
  if (!m_db.transaction()) { if (error) *error = m_db.lastError().text(); return {}; }
  QSqlQuery setQuery(m_db);
  setQuery.prepare("INSERT INTO target_sets(id,name,description,revision,created_at,updated_at) VALUES(?,?,?,?,?,?) "
                   "ON CONFLICT(id) DO UPDATE SET name=excluded.name,description=excluded.description,revision=excluded.revision,updated_at=excluded.updated_at");
  setQuery.addBindValue(id);
  setQuery.addBindValue(value.value("name").toString().trimmed());
  setQuery.addBindValue(description);
  setQuery.addBindValue(revision);
  setQuery.addBindValue(createdAt);
  setQuery.addBindValue(now);
  bool ok = setQuery.exec();
  QSqlQuery clear(m_db);
  clear.prepare("DELETE FROM targets WHERE target_set_id=?");
  clear.addBindValue(id);
  ok = ok && clear.exec();
  int position = 0;
  for (const auto &entry : value.value("targets").toArray()) {
    const QJsonObject target = entry.toObject();
    QSqlQuery targetQuery(m_db);
    targetQuery.prepare("INSERT INTO targets(id,target_set_id,position,label,url,enabled) VALUES(?,?,?,?,?,?)");
    targetQuery.addBindValue(target.value("id").toString(newId()));
    targetQuery.addBindValue(id);
    targetQuery.addBindValue(position++);
    QString label = target.value("label").toString().trimmed();
    if (label.isNull()) label = QStringLiteral("");
    targetQuery.addBindValue(label);
    targetQuery.addBindValue(target.value("url").toString().trimmed());
    targetQuery.addBindValue(target.value("enabled").toBool(true) ? 1 : 0);
    ok = ok && targetQuery.exec();
    if (!ok) { if (error) *error = targetQuery.lastError().text(); break; }
  }
  if (!ok || !m_db.commit()) {
    if (error && error->isEmpty()) *error = setQuery.lastError().text().isEmpty() ? m_db.lastError().text() : setQuery.lastError().text();
    m_db.rollback(); return {};
  }
  return targetSet(id);
}

bool ProjectStore::removeTargetSet(const QString &id, QString *error) {
  QSqlQuery dependencies(m_db);
  dependencies.prepare("SELECT name FROM schedules WHERE target_set_id=? ORDER BY name");
  dependencies.addBindValue(id);
  QStringList names;
  if (dependencies.exec()) while (dependencies.next()) names.append(dependencies.value(0).toString());
  if (!names.isEmpty()) {
    if (error) *error = "Target set is used by: " + names.join(", ");
    return false;
  }
  QSqlQuery query(m_db);
  query.prepare("DELETE FROM target_sets WHERE id=?");
  query.addBindValue(id);
  if (query.exec() && query.numRowsAffected() > 0) return true;
  if (error) *error = query.lastError().text().isEmpty() ? "Target set not found" : query.lastError().text();
  return false;
}

bool ProjectStore::insertJob(const JobRequest &request, QString *error) {
  if (!m_db.transaction()) {
    if (error) *error = m_db.lastError().text();
    return false;
  }
  const QString createdAt = utcNow();
  QSqlQuery query(m_db);
  query.prepare("INSERT INTO jobs(id,project_id,status,source,created_at,request_json) VALUES(?,?,?,?,?,?)");
  query.addBindValue(request.id);
  query.addBindValue(m_projectId);
  query.addBindValue("queued");
  query.addBindValue(request.source);
  query.addBindValue(createdAt);
  query.addBindValue(compactJson(toJson(request)));
  const QJsonObject queued{{"protocolVersion", 2}, {"sequence", 1}, {"timestamp", createdAt},
                           {"type", "job_queued"}, {"jobId", request.id}, {"status", "queued"}};
  if (query.exec() && appendEvent(request.id, queued) && m_db.commit()) return true;
  if (error) *error = query.lastError().text().isEmpty() ? m_db.lastError().text() : query.lastError().text();
  m_db.rollback();
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

bool ProjectStore::applyWorkerEvent(const QString &jobId, const QJsonObject &event, QString *error) {
  if (!m_db.transaction()) {
    if (error) *error = m_db.lastError().text();
    return false;
  }
  const QString type = event.value("type").toString();
  bool ok = true;
  if (type == "job_queued") {
    ok = updateJob(jobId, "queued");
  } else if (type == "job_preparing") {
    ok = updateJob(jobId, "preparing");
  } else if (type == "job_started") {
    ok = updateJob(jobId, "running");
  } else if (type == "job_cancelling") {
    ok = updateJob(jobId, "cancelling");
  } else if (type == "artifact_completed") {
    ok = insertArtifact(jobId, event.value("artifact").toObject()) &&
         updateJob(jobId, "running", {}, 1, 0);
  } else if (type == "artifact_failed") {
    ok = insertArtifact(jobId, event.value("artifact").toObject()) &&
         updateJob(jobId, "running", {}, 0, 1);
  } else if (type == "comparison_completed") {
    ok = insertComparison(event.value("comparison").toObject());
  } else if (type == "job_succeeded" || type == "job_partial" ||
             type == "job_failed" || type == "job_cancelled" || type == "job_interrupted") {
    const QString status = type == "job_succeeded" ? "succeeded" :
                           type == "job_partial" ? "partial" :
                           type == "job_cancelled" ? "cancelled" :
                           type == "job_interrupted" ? "interrupted" : "failed";
    ok = updateJob(jobId, status, event.value("message").toString());
  }
  ok = ok && appendEvent(jobId, event);
  if (ok && m_db.commit()) return true;
  if (error) *error = m_db.lastError().text();
  m_db.rollback();
  return false;
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

QJsonArray ProjectStore::queuedJobs() const {
  QJsonArray out;
  QSqlQuery query(m_db);
  query.prepare("SELECT * FROM jobs WHERE status='queued' ORDER BY created_at ASC, id ASC");
  if (!query.exec()) return out;
  while (query.next()) out.append(queryJob(query));
  return out;
}

bool ProjectStore::appendEvent(const QString &jobId, const QJsonObject &event) {
  QSqlQuery query(m_db);
  query.prepare("INSERT INTO events(job_id,sequence,type,created_at,event_json) VALUES(?,?,?,?,?)");
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
  query.prepare("INSERT OR REPLACE INTO artifacts(id,job_id,target_url,target_id,target_name,target_set_id,target_set_name,engine,viewport_id,viewport_name,capture_mode,format,relative_path,width,height,sha256,status,error,created_at,metadata_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  query.addBindValue(id);
  query.addBindValue(jobId);
  query.addBindValue(artifact.value("url").toString());
  query.addBindValue(artifact.value("targetId").toString());
  query.addBindValue(artifact.value("targetName").toString());
  query.addBindValue(artifact.value("targetSetId").toString());
  query.addBindValue(artifact.value("targetSetName").toString());
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
  query.prepare("INSERT INTO schedules(id,name,enabled,profile_id,target_set_id,urls_json,recurrence_json,last_run,next_run,last_status,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name,enabled=excluded.enabled,profile_id=excluded.profile_id,target_set_id=excluded.target_set_id,urls_json=excluded.urls_json,recurrence_json=excluded.recurrence_json,last_run=excluded.last_run,next_run=excluded.next_run,last_status=excluded.last_status,updated_at=excluded.updated_at");
  query.addBindValue(id);
  query.addBindValue(schedule.value("name").toString("Schedule"));
  query.addBindValue(schedule.value("enabled").toBool(true) ? 1 : 0);
  query.addBindValue(schedule.value("profileId").toString("default"));
  query.addBindValue(schedule.value("targetSetId").toString());
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
                           {"targetSetId", query.value("target_set_id").toString()},
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

QJsonArray ProjectStore::baselines() const {
  QJsonArray out;
  QSqlQuery query("SELECT comparison_key FROM baselines ORDER BY updated_at DESC", m_db);
  while (query.next()) out.append(baseline(query.value(0).toString()));
  return out;
}

bool ProjectStore::removeBaseline(const QString &key, QString *error) {
  QSqlQuery query(m_db);
  query.prepare("DELETE FROM baselines WHERE comparison_key=?");
  query.addBindValue(key);
  if (query.exec() && query.numRowsAffected() > 0) return true;
  if (error) *error = query.lastError().text().isEmpty() ? "Baseline not found" : query.lastError().text();
  return false;
}

bool ProjectStore::insertComparison(const QJsonObject &comparison) {
  QSqlQuery query(m_db);
  query.prepare("INSERT OR REPLACE INTO comparisons(id,job_id,comparison_key,baseline_artifact_id,current_artifact_id,status,mismatch_ratio,diff_relative_path,created_at,target_url,target_id,target_name,target_set_id,target_set_name,engine,viewport_id,viewport_name,capture_mode,format,analysis_width,analysis_height,analysis_scale,baseline_relative_path,metadata_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  query.addBindValue(comparison.value("id").toString(newId()));
  query.addBindValue(comparison.value("jobId").toString());
  query.addBindValue(comparison.value("comparisonKey").toString());
  query.addBindValue(comparison.value("baselineArtifactId").toString());
  query.addBindValue(comparison.value("currentArtifactId").toString());
  query.addBindValue(comparison.value("status").toString());
  query.addBindValue(comparison.value("mismatchRatio").toDouble());
  query.addBindValue(comparison.value("diffRelativePath").toString());
  query.addBindValue(comparison.value("createdAt").toString(utcNow()));
  query.addBindValue(comparison.value("url").toString());
  query.addBindValue(comparison.value("targetId").toString());
  query.addBindValue(comparison.value("targetName").toString());
  query.addBindValue(comparison.value("targetSetId").toString());
  query.addBindValue(comparison.value("targetSetName").toString());
  query.addBindValue(comparison.value("engine").toString());
  query.addBindValue(comparison.value("viewportId").toString());
  query.addBindValue(comparison.value("viewportName").toString());
  query.addBindValue(comparison.value("captureMode").toString());
  query.addBindValue(comparison.value("format").toString());
  query.addBindValue(comparison.value("analysisWidth").toInt());
  query.addBindValue(comparison.value("analysisHeight").toInt());
  query.addBindValue(comparison.value("analysisScale").toDouble(1.0));
  query.addBindValue(comparison.value("baselineRelativePath").toString());
  query.addBindValue(compactJson(comparison));
  return query.exec();
}

QJsonArray ProjectStore::comparisons(const QString &jobId) const {
  QJsonArray out;
  QSqlQuery query(m_db);
  const QString columns = "SELECT c.*,r.status AS review_status,r.note AS review_note,r.reviewed_at,r.updated_at AS review_updated_at,r.revision AS review_revision,r.promoted_artifact_id ";
  if (jobId.isEmpty()) {
    query.prepare(columns + "FROM comparisons c LEFT JOIN comparison_reviews r ON r.comparison_id=c.id ORDER BY c.created_at DESC,c.id DESC LIMIT 2000");
  } else {
    query.prepare(columns + "FROM comparisons c LEFT JOIN comparison_reviews r ON r.comparison_id=c.id WHERE c.job_id=? ORDER BY c.created_at ASC,c.id ASC");
    query.addBindValue(jobId);
  }
  if (!query.exec()) return out;
  while (query.next()) out.append(queryComparison(query));
  return out;
}

QJsonObject ProjectStore::comparison(const QString &id) const {
  QSqlQuery query(m_db);
  query.prepare("SELECT c.*,r.status AS review_status,r.note AS review_note,r.reviewed_at,r.updated_at AS review_updated_at,r.revision AS review_revision,r.promoted_artifact_id "
                "FROM comparisons c LEFT JOIN comparison_reviews r ON r.comparison_id=c.id WHERE c.id=?");
  query.addBindValue(id);
  return query.exec() && query.next() ? queryComparison(query) : QJsonObject{};
}

QJsonObject ProjectStore::setComparisonReview(const QString &comparisonId, const QString &status,
                                              const QString &note, int expectedRevision,
                                              QString *error) {
  if (!QStringList{"unreviewed", "accepted", "ignored"}.contains(status)) {
    if (error) *error = "Review status must be unreviewed, accepted, or ignored";
    return {};
  }
  const QJsonObject selected = comparison(comparisonId);
  if (selected.isEmpty()) { if (error) *error = "Comparison not found"; return {}; }
  if (selected.value("status").toString() == "matched") {
    if (error) *error = "Matched comparisons do not need review";
    return {};
  }
  const int revision = selected.value("review").toObject().value("revision").toInt();
  if (expectedRevision >= 0 && revision != expectedRevision) {
    if (error) *error = "Review changed since it was loaded";
    return {};
  }
  QSqlQuery query(m_db);
  query.prepare("INSERT INTO comparison_reviews(comparison_id,status,note,reviewed_at,updated_at,revision,promoted_artifact_id) VALUES(?,?,?,?,?,?,?) "
                "ON CONFLICT(comparison_id) DO UPDATE SET status=excluded.status,note=excluded.note,reviewed_at=excluded.reviewed_at,updated_at=excluded.updated_at,revision=excluded.revision,promoted_artifact_id=CASE WHEN excluded.promoted_artifact_id='' THEN comparison_reviews.promoted_artifact_id ELSE excluded.promoted_artifact_id END");
  const QString now = utcNow();
  QString normalizedNote = note.left(4096);
  if (normalizedNote.isNull()) normalizedNote = QStringLiteral("");
  query.addBindValue(comparisonId);
  query.addBindValue(status);
  query.addBindValue(normalizedNote);
  query.addBindValue(status == "unreviewed" ? QString{} : now);
  query.addBindValue(now);
  query.addBindValue(revision + 1);
  query.addBindValue(QString{});
  if (!query.exec()) { if (error) *error = query.lastError().text(); return {}; }
  return comparison(comparisonId);
}

QJsonObject ProjectStore::acceptComparison(const QString &comparisonId, const QString &baselineRelativePath,
                                           const QString &note, int expectedRevision, bool force,
                                           QString *error) {
  const QJsonObject selected = comparison(comparisonId);
  if (selected.isEmpty()) { if (error) *error = "Comparison not found"; return {}; }
  if (selected.value("status").toString() == "matched" || selected.value("status").toString() == "error") {
    if (error) *error = "This comparison cannot be accepted as a baseline";
    return {};
  }
  const int revision = selected.value("review").toObject().value("revision").toInt();
  if (expectedRevision >= 0 && revision != expectedRevision) {
    if (error) *error = "Review changed since it was loaded";
    return {};
  }
  const QString key = selected.value("comparisonKey").toString();
  const QString currentArtifact = selected.value("currentArtifactId").toString();
  const QString expectedBaseline = selected.value("baselineArtifactId").toString();
  const QJsonObject activeBaseline = baseline(key);
  if (!force && activeBaseline.value("artifactId").toString() != expectedBaseline) {
    if (error) *error = "The baseline changed since this comparison was created";
    return {};
  }
  if (!m_db.transaction()) { if (error) *error = m_db.lastError().text(); return {}; }
  QSqlQuery baselineQuery(m_db);
  baselineQuery.prepare("INSERT INTO baselines(comparison_key,artifact_id,relative_path,updated_at) VALUES(?,?,?,?) "
                        "ON CONFLICT(comparison_key) DO UPDATE SET artifact_id=excluded.artifact_id,relative_path=excluded.relative_path,updated_at=excluded.updated_at");
  baselineQuery.addBindValue(key); baselineQuery.addBindValue(currentArtifact);
  baselineQuery.addBindValue(baselineRelativePath); baselineQuery.addBindValue(utcNow());
  QSqlQuery reviewQuery(m_db);
  reviewQuery.prepare("INSERT INTO comparison_reviews(comparison_id,status,note,reviewed_at,updated_at,revision,promoted_artifact_id) VALUES(?,?,?,?,?,?,?) "
                      "ON CONFLICT(comparison_id) DO UPDATE SET status=excluded.status,note=excluded.note,reviewed_at=excluded.reviewed_at,updated_at=excluded.updated_at,revision=excluded.revision,promoted_artifact_id=excluded.promoted_artifact_id");
  const QString now = utcNow();
  QString normalizedNote = note.left(4096);
  if (normalizedNote.isNull()) normalizedNote = QStringLiteral("");
  reviewQuery.addBindValue(comparisonId); reviewQuery.addBindValue("accepted");
  reviewQuery.addBindValue(normalizedNote); reviewQuery.addBindValue(now); reviewQuery.addBindValue(now);
  reviewQuery.addBindValue(revision + 1); reviewQuery.addBindValue(currentArtifact);
  if (!baselineQuery.exec() || !reviewQuery.exec() || !m_db.commit()) {
    if (error) *error = !baselineQuery.lastError().text().isEmpty() ? baselineQuery.lastError().text() :
                        !reviewQuery.lastError().text().isEmpty() ? reviewQuery.lastError().text() : m_db.lastError().text();
    m_db.rollback(); return {};
  }
  return comparison(comparisonId);
}

QJsonObject ProjectStore::dashboard() const {
  auto scalar = [this](const QString &sql) {
    QSqlQuery query(m_db);
    return query.exec(sql) && query.next() ? query.value(0).toInt() : 0;
  };
  QJsonObject out{{"activeJobs", scalar("SELECT COUNT(*) FROM jobs WHERE status IN ('queued','preparing','running','cancelling')")},
                  {"failedRuns", scalar("SELECT COUNT(*) FROM jobs WHERE status IN ('failed','partial','interrupted') AND created_at>=datetime('now','-7 days')")},
                  {"needsReview", scalar("SELECT COUNT(*) FROM comparisons c LEFT JOIN comparison_reviews r ON r.comparison_id=c.id WHERE c.status IN ('changed','dimensions_changed','missing_baseline','error') AND COALESCE(r.status,'unreviewed')='unreviewed'")},
                  {"missingBaselines", scalar("SELECT COUNT(*) FROM comparisons c LEFT JOIN comparison_reviews r ON r.comparison_id=c.id WHERE c.status='missing_baseline' AND COALESCE(r.status,'unreviewed')='unreviewed'")}};
  QSqlQuery next(m_db);
  if (next.exec("SELECT id,name,next_run FROM schedules WHERE enabled=1 AND next_run<>'' ORDER BY next_run LIMIT 1") && next.next()) {
    out.insert("nextSchedule", QJsonObject{{"id", next.value(0).toString()}, {"name", next.value(1).toString()},
                                           {"nextRun", next.value(2).toString()}});
  }
  QJsonArray recentJobs = jobs(5);
  out.insert("recentJobs", recentJobs);
  QJsonArray recentReviews;
  for (const auto &value : comparisons()) {
    const QJsonObject comparisonValue = value.toObject();
    if (comparisonValue.value("status").toString() != "matched" &&
        comparisonValue.value("review").toObject().value("status").toString("unreviewed") == "unreviewed") {
      recentReviews.append(comparisonValue);
      if (recentReviews.size() == 5) break;
    }
  }
  out.insert("recentReviews", recentReviews);
  out.insert("targetSets", targetSets());
  return out;
}

QJsonObject ProjectStore::parseObject(const QVariant &value) {
  return QJsonDocument::fromJson(value.toByteArray()).object();
}

} // namespace CyberSnapper
