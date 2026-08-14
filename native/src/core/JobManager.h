#pragma once

#include "core/Models.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QQueue>

namespace CyberSnapper {

class ProjectStore;

class JobManager final : public QObject {
  Q_OBJECT
public:
  explicit JobManager(QObject *parent = nullptr);
  ~JobManager() override;

  QString submit(ProjectStore *store, JobRequest request, QString *error = nullptr);
  bool recover(ProjectStore *store, JobRequest request, QString *error = nullptr);
  bool cancel(const QString &jobId, QString *error = nullptr);
  bool hasActiveJobs() const;
  int queuedCount() const;
  int activeCount() const;
  void setMaximumActiveJobs(int count);
  void shutdown();

signals:
  void eventPublished(const QString &projectId, const QJsonObject &event);
  void queueChanged(int queued, int active);

private:
  struct PendingJob {
    ProjectStore *store = nullptr;
    JobRequest request;
  };

  struct ActiveJob {
    ProjectStore *store = nullptr;
    JobRequest request;
    QProcess *process = nullptr;
    QByteArray stdoutBuffer;
    qint64 fallbackSequence = 0;
    bool terminalSeen = false;
    bool startedSeen = false;
    qint64 lastEventMs = 0;
  };

  QQueue<PendingJob> m_queue;
  QHash<QString, ActiveJob *> m_active;
  int m_maximumActive = 1;
  bool m_shuttingDown = false;

  void startNext();
  void startJob(const PendingJob &pending);
  void readWorkerOutput(ActiveJob *active);
  void handleWorkerEvent(ActiveJob *active, QJsonObject event);
  void finishProcess(ActiveJob *active, int exitCode, QProcess::ExitStatus exitStatus);
  void checkLiveness(const QString &jobId);
  void publish(ProjectStore *store, QJsonObject event);
  void failBeforeStart(ProjectStore *store, const JobRequest &request, const QString &message);
  static QString validate(const JobRequest &request);
};

} // namespace CyberSnapper
