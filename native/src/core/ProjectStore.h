#pragma once

#include "core/Models.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>
#include <QSqlDatabase>
#include <QString>
#include <memory>

namespace CyberSnapper {

class ProjectStore final {
public:
  ProjectStore();
  ~ProjectStore();

  ProjectStore(const ProjectStore &) = delete;
  ProjectStore &operator=(const ProjectStore &) = delete;

  bool open(const QString &root, const QString &requestedName = {}, QString *error = nullptr);
  bool isOpen() const;
  QString root() const;
  QString projectId() const;
  QString projectName() const;

  CaptureProfile profile(const QString &id = "default") const;
  QJsonArray profiles() const;
  bool saveProfile(const CaptureProfile &profile, QString *error = nullptr);

  bool insertJob(const JobRequest &request, QString *error = nullptr);
  bool updateJob(const QString &jobId, const QString &status, const QString &error = {},
                 int completedDelta = 0, int failedDelta = 0);
  QJsonObject job(const QString &jobId) const;
  QJsonArray jobs(int limit = 200) const;

  bool appendEvent(const QString &jobId, const QJsonObject &event);
  QJsonArray events(const QString &jobId, qint64 afterSequence = -1) const;
  bool insertArtifact(const QString &jobId, const QJsonObject &artifact);
  QJsonArray artifacts(const QString &jobId = {}) const;
  QJsonObject artifact(const QString &artifactId) const;

  bool upsertSchedule(const QJsonObject &schedule, QString *error = nullptr);
  bool removeSchedule(const QString &scheduleId);
  QJsonArray schedules(bool enabledOnly = false) const;
  bool updateScheduleRun(const QString &scheduleId, const QString &lastRun, const QString &nextRun,
                         const QString &lastStatus);

  bool setBaseline(const QString &key, const QString &artifactId, const QString &relativePath = {});
  QJsonObject baseline(const QString &key) const;
  bool insertComparison(const QJsonObject &comparison);
  QJsonArray comparisons(const QString &jobId) const;

private:
  bool migrate(QString *error);
  bool writeManifest(QString *error);
  bool execute(const QString &sql, QString *error = nullptr) const;
  static QJsonObject parseObject(const QVariant &value);

  QString m_root;
  QString m_projectId;
  QString m_projectName;
  QString m_connectionName;
  QSqlDatabase m_db;
  std::unique_ptr<QLockFile> m_lock;
};

} // namespace CyberSnapper
