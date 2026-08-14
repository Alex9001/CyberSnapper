#include "core/Rpc.h"

#include "core/Models.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <utility>

namespace CyberSnapper {

namespace {

constexpr quint32 kMaxFrame = 16 * 1024 * 1024;

bool takeFrame(QByteArray &buffer, QJsonObject *message, QString *error) {
  if (buffer.size() < 4) return false;
  const auto *raw = reinterpret_cast<const uchar *>(buffer.constData());
  const quint32 size = (quint32(raw[0]) << 24) | (quint32(raw[1]) << 16) |
                       (quint32(raw[2]) << 8) | quint32(raw[3]);
  if (size == 0 || size > kMaxFrame) {
    if (error) *error = QStringLiteral("Invalid RPC frame length: %1").arg(size);
    buffer.clear();
    return false;
  }
  if (buffer.size() < int(size + 4)) return false;
  const QByteArray payload = buffer.mid(4, size);
  buffer.remove(0, size + 4);
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(payload, &parseError);
  if (!document.isObject()) {
    if (error) *error = QStringLiteral("Invalid RPC JSON: %1").arg(parseError.errorString());
    return false;
  }
  *message = document.object();
  return true;
}

QJsonObject rpcError(const QString &code, const QString &message) {
  return {{"code", code}, {"message", message}};
}

} // namespace

QByteArray encodeRpcFrame(const QJsonObject &message) {
  const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
  QByteArray frame;
  frame.resize(4);
  const quint32 size = payload.size();
  frame[0] = char((size >> 24) & 0xff);
  frame[1] = char((size >> 16) & 0xff);
  frame[2] = char((size >> 8) & 0xff);
  frame[3] = char(size & 0xff);
  frame.append(payload);
  return frame;
}

RpcPeer::RpcPeer(QLocalSocket *socket, QObject *parent) : QObject(parent), m_socket(socket) {
  m_socket->setParent(this);
  connect(m_socket, &QLocalSocket::readyRead, this, &RpcPeer::readAvailable);
  connect(m_socket, &QLocalSocket::disconnected, this, [this] { emit disconnected(this); });
}

void RpcPeer::send(const QJsonObject &message) {
  if (m_socket->state() == QLocalSocket::ConnectedState) m_socket->write(encodeRpcFrame(message));
}

QLocalSocket *RpcPeer::socket() const { return m_socket; }

void RpcPeer::readAvailable() {
  m_buffer.append(m_socket->readAll());
  while (!m_buffer.isEmpty()) {
    QJsonObject message;
    QString error;
    const int before = m_buffer.size();
    if (takeFrame(m_buffer, &message, &error)) {
      emit messageReceived(message);
      continue;
    }
    if (!error.isEmpty()) {
      emit protocolError(error);
      m_socket->disconnectFromServer();
    }
    if (before == m_buffer.size()) break;
  }
}

RpcServer::RpcServer(QObject *parent) : QObject(parent) {
  connect(&m_server, &QLocalServer::newConnection, this, &RpcServer::acceptConnections);
}

bool RpcServer::listen(const QString &name, Handler handler, QString *error) {
  m_handler = std::move(handler);
  m_server.setSocketOptions(QLocalServer::UserAccessOption);
  if (!m_server.listen(name)) {
    QLocalSocket probe;
    probe.connectToServer(name, QIODevice::ReadWrite);
    if (probe.waitForConnected(250)) {
      if (error) *error = "A CyberSnapper agent is already listening";
      probe.disconnectFromServer();
      return false;
    }
    QLocalServer::removeServer(name);
    if (!m_server.listen(name)) {
      if (error) *error = m_server.errorString();
      return false;
    }
  }
  return true;
}

void RpcServer::acceptConnections() {
  while (QLocalSocket *socket = m_server.nextPendingConnection()) {
    auto *peer = new RpcPeer(socket, this);
    m_peers.append(peer);
    connect(peer, &RpcPeer::messageReceived, this, [this, peer](const QJsonObject &request) {
      const QString id = request.value("id").toString();
      const QString method = request.value("method").toString();
      if (request.value("v").toInt() != 1 || id.isEmpty() || method.isEmpty()) {
        peer->send({{"v", 1}, {"id", id}, {"error", rpcError("invalid_request", "RPC v1 requires id and method")}});
        return;
      }
      try {
        const QJsonObject result = m_handler ? m_handler(method, request.value("params").toObject()) : QJsonObject{};
        if (result.contains("_error")) {
          peer->send({{"v", 1}, {"id", id}, {"error", result.value("_error")}});
        } else {
          peer->send({{"v", 1}, {"id", id}, {"result", result}});
        }
      } catch (const std::exception &exception) {
        peer->send({{"v", 1}, {"id", id}, {"error", rpcError("internal_error", exception.what())}});
      }
    });
    connect(peer, &RpcPeer::disconnected, this, [this](RpcPeer *dead) {
      m_peers.removeAll(dead);
      dead->deleteLater();
      emit clientCountChanged(m_peers.size());
    });
    emit clientCountChanged(m_peers.size());
  }
}

void RpcServer::broadcast(const QString &event, const QJsonObject &data) {
  const QJsonObject message{{"v", 1}, {"event", event}, {"data", data}};
  for (auto *peer : std::as_const(m_peers)) peer->send(message);
}

int RpcServer::connectionCount() const { return m_peers.size(); }

RpcClient::RpcClient(QObject *parent) : QObject(parent) {
  connect(&m_socket, &QLocalSocket::connected, this, &RpcClient::connected);
  connect(&m_socket, &QLocalSocket::disconnected, this, &RpcClient::disconnected);
  connect(&m_socket, &QLocalSocket::readyRead, this, &RpcClient::readAvailable);
  connect(&m_socket, &QLocalSocket::errorOccurred, this,
          [this](QLocalSocket::LocalSocketError) { emit connectionError(m_socket.errorString()); });
}

void RpcClient::connectToAgent(const QString &serverName) {
  if (m_socket.state() != QLocalSocket::UnconnectedState) m_socket.abort();
  m_socket.connectToServer(serverName, QIODevice::ReadWrite);
}

bool RpcClient::isConnected() const { return m_socket.state() == QLocalSocket::ConnectedState; }

QString RpcClient::call(const QString &method, const QJsonObject &params, Callback callback) {
  const QString id = newId();
  if (callback) m_callbacks.insert(id, std::move(callback));
  m_socket.write(encodeRpcFrame({{"v", 1}, {"id", id}, {"method", method}, {"params", params}}));
  return id;
}

void RpcClient::disconnectFromAgent() { m_socket.disconnectFromServer(); }

void RpcClient::readAvailable() {
  m_buffer.append(m_socket.readAll());
  while (!m_buffer.isEmpty()) {
    QJsonObject message;
    QString error;
    const int before = m_buffer.size();
    if (takeFrame(m_buffer, &message, &error)) {
      if (message.contains("event")) {
        emit eventReceived(message.value("event").toString(), message.value("data").toObject());
      } else {
        const QString id = message.value("id").toString();
        auto callback = m_callbacks.take(id);
        if (callback) callback(message.value("result").toObject(), message.value("error").toObject());
      }
      continue;
    }
    if (!error.isEmpty()) emit connectionError(error);
    if (before == m_buffer.size()) break;
  }
}

QJsonObject blockingRpcCall(const QString &serverName, const QString &method,
                            const QJsonObject &params, int timeoutMs, QString *error) {
  QLocalSocket socket;
  socket.connectToServer(serverName, QIODevice::ReadWrite);
  if (!socket.waitForConnected(timeoutMs)) {
    if (error) *error = socket.errorString();
    return {};
  }
  const QString id = newId();
  socket.write(encodeRpcFrame({{"v", 1}, {"id", id}, {"method", method}, {"params", params}}));
  if (!socket.waitForBytesWritten(timeoutMs)) {
    if (error) *error = socket.errorString();
    return {};
  }
  QByteArray buffer;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(qMin(250, timeoutMs - int(timer.elapsed())))) continue;
    buffer.append(socket.readAll());
    while (!buffer.isEmpty()) {
      QJsonObject response;
      QString frameError;
      const int before = buffer.size();
      if (takeFrame(buffer, &response, &frameError)) {
        if (response.value("id").toString() != id) continue;
        if (response.contains("error")) {
          if (error) *error = response.value("error").toObject().value("message").toString();
          return {};
        }
        return response.value("result").toObject();
      }
      if (!frameError.isEmpty()) {
        if (error) *error = frameError;
        return {};
      }
      if (before == buffer.size()) break;
    }
  }
  if (error) *error = "Timed out waiting for CyberSnapper agent";
  return {};
}

} // namespace CyberSnapper
