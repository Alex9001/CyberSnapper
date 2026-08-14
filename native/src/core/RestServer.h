#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace httplib { class Server; }

namespace CyberSnapper {

class RestServer final : public QObject {
  Q_OBJECT
public:
  using Handler = std::function<QJsonObject(const QString &, const QJsonObject &)>;

  explicit RestServer(QObject *parent = nullptr);
  ~RestServer() override;

  bool start(quint16 port, const QByteArray &tokenHash, Handler handler, QString *error = nullptr);
  void stop();
  bool isRunning() const;
  quint16 port() const;
  void publishEvent(const QJsonObject &event);

signals:
  void serverError(const QString &message);

private:
  struct EventStream {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<QJsonObject> events;
    bool terminal = false;
  };

  std::unique_ptr<httplib::Server> m_server;
  std::thread m_thread;
  Handler m_handler;
  QByteArray m_tokenHash;
  quint16 m_port = 0;
  std::atomic_bool m_stopping = false;
  mutable std::mutex m_streamsMutex;
  std::unordered_map<std::string, std::shared_ptr<EventStream>> m_streams;

  void configureRoutes();
  std::shared_ptr<EventStream> streamFor(const QString &jobId);
};

} // namespace CyberSnapper
