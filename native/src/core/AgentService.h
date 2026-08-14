#pragma once

#include "core/JobManager.h"
#include "core/RestServer.h"
#include "core/Scheduler.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <memory>

namespace CyberSnapper {

class ProjectStore;

class AgentService final : public QObject {
  Q_OBJECT
public:
  explicit AgentService(QObject *parent = nullptr);
  ~AgentService() override;

  bool start(QString *error = nullptr);
  void shutdown();
  QJsonObject handle(const QString &method, const QJsonObject &params);
  QJsonObject invokeThreadSafe(const QString &method, const QJsonObject &params);

signals:
  void eventPublished(const QString &event, const QJsonObject &data);
  void notificationRequested(const QString &title, const QString &message);
  void quitRequested();

private:
  QSettings m_settings;
  QHash<QString, std::shared_ptr<ProjectStore>> m_projects;
  QString m_activeProjectId;
  JobManager m_jobs;
  Scheduler m_scheduler;
  RestServer m_rest;
  bool m_started = false;

  QList<ProjectStore *> stores() const;
  ProjectStore *project(const QString &projectId = {}) const;
  ProjectStore *projectForJob(const QString &jobId) const;
  ProjectStore *projectForArtifact(const QString &artifactId) const;
  ProjectStore *openProject(const QString &root, const QString &name, QString *error);
  void rememberProject(const QString &root);
  bool configureApi(bool enabled, QString *plainToken, QString *error);
  static QByteArray tokenHash(const QString &token);
  static QString generateToken();
  static QJsonObject failure(const QString &code, const QString &message, int status = 400);
};

} // namespace CyberSnapper
