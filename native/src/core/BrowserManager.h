#pragma once

#include <QHash>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QTimer>
#include <optional>

class QProcess;

namespace CyberSnapper {

class BrowserManager final : public QObject {
  Q_OBJECT
public:
  explicit BrowserManager(QObject *parent = nullptr);
  ~BrowserManager() override;

  QJsonObject status();
  QJsonObject install(const QString &engine, bool force = false);
  QJsonObject verify(const QString &engine);
  QJsonObject cancel(const QString &installId);
  QJsonObject cancelAll();
  QJsonObject task(const QString &installId) const;
  bool hasPendingOperations() const;
  int queuedCount() const;
  void shutdown();

signals:
  void progressPublished(const QJsonObject &data);
  void finishedPublished(const QJsonObject &data);

private:
  struct Task {
    QString id;
    QString engine;
    QString operation;
    bool force = false;
  };

  QQueue<Task> m_queue;
  std::optional<Task> m_active;
  QProcess *m_process = nullptr;
  QByteArray m_stdoutBuffer;
  QByteArray m_stderrBuffer;
  QStringList m_logs;
  QJsonObject m_workerResult;
  QHash<QString, QJsonObject> m_tasks;
  QHash<QString, QJsonObject> m_engineResults;
  QStringList m_taskOrder;
  bool m_shuttingDown = false;
  QElapsedTimer m_elapsed;
  QTimer m_progressTimer;

  static bool validEngine(const QString &engine);
  static QJsonObject failure(const QString &code, const QString &message, int status = 400);
  QJsonObject enqueue(const QString &engine, const QString &operation, bool force);
  void updateQueuePositions();
  void startNext();
  void consumeOutput(QByteArray &buffer, const QByteArray &chunk, bool structured);
  void processLine(const QByteArray &line, bool structured);
  void appendLog(const QString &line);
  void finishActive(QJsonObject result, int exitCode = -1);
  void finishQueued(const Task &task, const QString &state, const QString &message);
};

} // namespace CyberSnapper
