#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <functional>

namespace CyberSnapper {

QByteArray encodeRpcFrame(const QJsonObject &message);

class RpcPeer final : public QObject {
  Q_OBJECT
public:
  explicit RpcPeer(QLocalSocket *socket, QObject *parent = nullptr);
  void send(const QJsonObject &message);
  QLocalSocket *socket() const;

signals:
  void messageReceived(const QJsonObject &message);
  void disconnected(RpcPeer *peer);
  void protocolError(const QString &message);

private slots:
  void readAvailable();

private:
  QLocalSocket *m_socket;
  QByteArray m_buffer;
};

class RpcServer final : public QObject {
  Q_OBJECT
public:
  using Handler = std::function<QJsonObject(const QString &, const QJsonObject &)>;

  explicit RpcServer(QObject *parent = nullptr);
  bool listen(const QString &name, Handler handler, QString *error = nullptr);
  void broadcast(const QString &event, const QJsonObject &data);
  int connectionCount() const;

signals:
  void clientCountChanged(int count);

private slots:
  void acceptConnections();

private:
  QLocalServer m_server;
  QList<RpcPeer *> m_peers;
  Handler m_handler;
};

class RpcClient final : public QObject {
  Q_OBJECT
public:
  using Callback = std::function<void(const QJsonObject &, const QJsonObject &)>;

  explicit RpcClient(QObject *parent = nullptr);
  void connectToAgent(const QString &serverName);
  bool isConnected() const;
  QString call(const QString &method, const QJsonObject &params = {}, Callback callback = {});
  void disconnectFromAgent();

signals:
  void connected();
  void disconnected();
  void connectionError(const QString &message);
  void eventReceived(const QString &event, const QJsonObject &data);

private:
  QLocalSocket m_socket;
  QByteArray m_buffer;
  QHash<QString, Callback> m_callbacks;

  void readAvailable();
};

QJsonObject blockingRpcCall(const QString &serverName, const QString &method,
                            const QJsonObject &params, int timeoutMs, QString *error = nullptr);

} // namespace CyberSnapper
