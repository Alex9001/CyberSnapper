#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

namespace CyberSnapper {

class JobManager;
class ProjectStore;

class Scheduler final : public QObject {
  Q_OBJECT
public:
  using StoreProvider = std::function<QList<ProjectStore *>()>;

  Scheduler(JobManager *jobs, StoreProvider stores, QObject *parent = nullptr);
  void start();
  void stop();
  void checkNow();

  static QDateTime nextOccurrence(const QJsonObject &recurrence, const QDateTime &afterUtc,
                                  QString *error = nullptr);

signals:
  void scheduleEvent(const QString &projectId, const QJsonObject &event);

private:
  JobManager *m_jobs;
  StoreProvider m_stores;
  QTimer m_timer;
  QSet<QString> m_inFlightSchedules;
  QHash<QString, QString> m_jobToSchedule;

  void handleJobEvent(const QString &projectId, const QJsonObject &event);
};

} // namespace CyberSnapper
