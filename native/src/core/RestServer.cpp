#include "core/RestServer.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QStringList>
#include <httplib.h>
#include <chrono>

namespace CyberSnapper {

namespace {

std::string jsonBytes(const QJsonValue &value) {
  if (value.isArray()) return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).toStdString();
  return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString();
}

QJsonObject parseBody(const httplib::Request &request, QString *error) {
  if (request.body.size() > 1024 * 1024) {
    if (error) *error = "Request body exceeds 1 MiB";
    return {};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(request.body), &parseError);
  if (!document.isObject()) {
    if (error) *error = "Invalid JSON: " + parseError.errorString();
    return {};
  }
  return document.object();
}

void sendJson(httplib::Response &response, int status, const QJsonValue &value) {
  response.status = status;
  response.set_header("Cache-Control", "no-store");
  response.set_content(jsonBytes(value), "application/json; charset=utf-8");
}

void sendError(httplib::Response &response, int status, const QString &code, const QString &message) {
  sendJson(response, status, QJsonObject{{"error", QJsonObject{{"code", code}, {"message", message}}}});
}

bool isTerminal(const QString &type) {
  return QStringList{"job_succeeded", "job_partial", "job_failed", "job_cancelled", "job_interrupted"}.contains(type);
}

} // namespace

RestServer::RestServer(QObject *parent) : QObject(parent) {}

RestServer::~RestServer() { stop(); }

bool RestServer::start(quint16 portValue, const QByteArray &tokenHash, Handler handler, QString *error) {
  stop();
  m_handler = std::move(handler);
  m_stopping = false;
  m_tokenHash = tokenHash;
  m_server = std::make_unique<httplib::Server>();
  m_server->set_payload_max_length(1024 * 1024);
  m_server->set_read_timeout(10, 0);
  m_server->set_write_timeout(30, 0);
  m_server->set_keep_alive_timeout(5);
  configureRoutes();
  if (!m_server->bind_to_port("127.0.0.1", portValue)) {
    if (error) *error = QStringLiteral("Could not bind REST API to 127.0.0.1:%1").arg(portValue);
    m_server.reset();
    return false;
  }
  // cpp-httplib's bind_to_port returns success, not the selected port. The
  // server binds the explicit port above, so preserve that value for API
  // status responses and clients instead of reporting boolean true as port 1.
  m_port = portValue;
  m_thread = std::thread([this] {
    if (!m_server->listen_after_bind() && !m_stopping) emit serverError("REST API server stopped unexpectedly");
  });
  return true;
}

void RestServer::stop() {
  m_stopping = true;
  if (m_server) m_server->stop();
  {
    std::lock_guard lock(m_streamsMutex);
    for (auto &[_, stream] : m_streams) {
      std::lock_guard streamLock(stream->mutex);
      stream->terminal = true;
      stream->changed.notify_all();
    }
  }
  if (m_thread.joinable()) m_thread.join();
  m_server.reset();
  m_port = 0;
  std::lock_guard lock(m_streamsMutex);
  m_streams.clear();
}

bool RestServer::isRunning() const { return m_server && m_port != 0; }
quint16 RestServer::port() const { return m_port; }

std::shared_ptr<RestServer::EventStream> RestServer::streamFor(const QString &jobId) {
  std::lock_guard lock(m_streamsMutex);
  const std::string key = jobId.toStdString();
  auto it = m_streams.find(key);
  if (it != m_streams.end()) return it->second;
  auto stream = std::make_shared<EventStream>();
  m_streams.emplace(key, stream);
  return stream;
}

void RestServer::publishEvent(const QJsonObject &event) {
  const QString jobId = event.value("jobId").toString();
  if (jobId.isEmpty()) return;
  auto stream = streamFor(jobId);
  {
    std::lock_guard lock(stream->mutex);
    stream->events.push_back(event);
    if (stream->events.size() > 10000) stream->events.erase(stream->events.begin(), stream->events.begin() + 1000);
    if (isTerminal(event.value("type").toString())) stream->terminal = true;
  }
  stream->changed.notify_all();
}

void RestServer::configureRoutes() {
  auto authorized = [this](const httplib::Request &request) {
    const std::string header = request.get_header_value("Authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (!header.starts_with(prefix)) return false;
    const QByteArray supplied = QByteArray::fromStdString(header.substr(prefix.size()));
    const QByteArray candidate = QCryptographicHash::hash(supplied, QCryptographicHash::Sha256).toHex();
    if (candidate.size() != m_tokenHash.size()) return false;
    uchar difference = 0;
    for (qsizetype index = 0; index < candidate.size(); ++index) {
      difference |= uchar(candidate.at(index)) ^ uchar(m_tokenHash.at(index));
    }
    return difference == 0;
  };
  auto invoke = [this](const QString &method, const QJsonObject &params) {
    return m_handler ? m_handler(method, params) : QJsonObject{{"_error", QJsonObject{{"code", "unavailable"}, {"message", "Agent unavailable"}}}};
  };
  auto requireAuth = [authorized](const httplib::Request &request, httplib::Response &response) {
    if (authorized(request)) return true;
    sendError(response, 401, "unauthorized", "Use Authorization: Bearer <token>");
    return false;
  };
  auto resultResponse = [](httplib::Response &response, const QJsonObject &result, int successStatus = 200) {
    if (result.contains("_error")) {
      const auto error = result.value("_error").toObject();
      sendError(response, error.value("status").toInt(400), error.value("code").toString("request_failed"),
                error.value("message").toString("Request failed"));
    } else {
      sendJson(response, successStatus, result);
    }
  };

  m_server->Get("/api/v1/health", [](const httplib::Request &, httplib::Response &response) {
    sendJson(response, 200, QJsonObject{{"ok", true}, {"version", CYBERSNAPPER_VERSION}, {"apiVersion", 1}});
  });
  m_server->Get("/api/v1/projects", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("project.list", {}));
  });
  m_server->Post("/api/v1/projects", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError;
    const auto body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    resultResponse(response, invoke("project.create", body), 201);
  });
  m_server->Post("/api/v1/projects/open", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError; const auto body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    resultResponse(response, invoke("project.open", body));
  });
  m_server->Get(R"(/api/v1/projects/([0-9A-Za-z-]+)/profiles)",
                [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("profile.list", {{"projectId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Put(R"(/api/v1/projects/([0-9A-Za-z-]+)/profiles/([0-9A-Za-z-]+))",
                [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError;
    QJsonObject profile = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    profile.insert("id", QString::fromStdString(request.matches[2]));
    resultResponse(response, invoke("profile.save", {{"projectId", QString::fromStdString(request.matches[1])},
                                                       {"profile", profile}}));
  });
  m_server->Delete(R"(/api/v1/projects/([0-9A-Za-z-]+)/profiles/([0-9A-Za-z-]+))",
                   [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("profile.remove", {{"projectId", QString::fromStdString(request.matches[1])},
                                                          {"profileId", QString::fromStdString(request.matches[2])}}));
  });
  m_server->Patch(R"(/api/v1/projects/([0-9A-Za-z-]+)/settings)",
                  [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError; QJsonObject body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    body.insert("projectId", QString::fromStdString(request.matches[1]));
    resultResponse(response, invoke("project.settings.set", body));
  });
  m_server->Get(R"(/api/v1/projects/([0-9A-Za-z-]+)/dashboard)",
                [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("dashboard.get", {{"projectId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Get(R"(/api/v1/projects/([0-9A-Za-z-]+)/target-sets)",
                [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("targetSet.list", {{"projectId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Get(R"(/api/v1/projects/([0-9A-Za-z-]+)/target-sets/([0-9A-Za-z-]+))",
                [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("targetSet.get", {{"projectId", QString::fromStdString(request.matches[1])},
                                                        {"targetSetId", QString::fromStdString(request.matches[2])}}));
  });
  m_server->Put(R"(/api/v1/projects/([0-9A-Za-z-]+)/target-sets/([0-9A-Za-z-]+))",
                [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError; QJsonObject targetSet = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    targetSet.insert("id", QString::fromStdString(request.matches[2]));
    resultResponse(response, invoke("targetSet.save", {{"projectId", QString::fromStdString(request.matches[1])},
                                                         {"targetSet", targetSet}}));
  });
  m_server->Delete(R"(/api/v1/projects/([0-9A-Za-z-]+)/target-sets/([0-9A-Za-z-]+))",
                   [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("targetSet.remove", {{"projectId", QString::fromStdString(request.matches[1])},
                                                           {"targetSetId", QString::fromStdString(request.matches[2])}}));
  });
  m_server->Patch(R"(/api/v1/projects/([0-9A-Za-z-]+)/comparisons/([0-9A-Za-z-]+)/review)",
                  [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError; QJsonObject body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    body.insert("projectId", QString::fromStdString(request.matches[1]));
    body.insert("comparisonId", QString::fromStdString(request.matches[2]));
    resultResponse(response, invoke("comparison.review.set", body));
  });
  m_server->Post(R"(/api/v1/projects/([0-9A-Za-z-]+)/comparisons/review-batch)",
                 [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError; QJsonObject body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    body.insert("projectId", QString::fromStdString(request.matches[1]));
    resultResponse(response, invoke("comparison.review.batch", body));
  });
  m_server->Post("/api/v1/jobs", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError;
    const auto body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    resultResponse(response, invoke("job.submit", body), 202);
  });
  m_server->Get("/api/v1/jobs", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QJsonObject params;
    if (request.has_param("projectId")) params.insert("projectId", QString::fromStdString(request.get_param_value("projectId")));
    resultResponse(response, invoke("job.list", params));
  });
  m_server->Get(R"(/api/v1/jobs/([0-9a-f-]+))", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("job.get", {{"jobId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Post(R"(/api/v1/jobs/([0-9a-f-]+)/(cancel|retry))", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    const QString action = QString::fromStdString(request.matches[2]);
    resultResponse(response, invoke(action == "cancel" ? "job.cancel" : "job.retry",
                                    {{"jobId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Get(R"(/api/v1/jobs/([0-9a-f-]+)/artifacts)", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("artifact.list", {{"jobId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Get(R"(/api/v1/jobs/([0-9a-f-]+)/comparisons)", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("comparison.list", {{"jobId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Put(R"(/api/v1/artifacts/([0-9a-f-]+)/baseline)", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("baseline.set", {{"artifactId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->Get("/api/v1/baselines", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QJsonObject params; if (request.has_param("projectId")) params.insert("projectId", QString::fromStdString(request.get_param_value("projectId")));
    resultResponse(response, invoke("baseline.list", params));
  });
  m_server->Delete("/api/v1/baselines", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError; const QJsonObject body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    resultResponse(response, invoke("baseline.remove", body));
  });
  m_server->Get("/api/v1/comparisons", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QJsonObject params;
    for (const std::string &key : {"projectId", "status", "reviewStatus", "targetSetId", "engine", "viewportId", "search", "cursor"}) {
      if (request.has_param(key)) params.insert(QString::fromStdString(key), QString::fromStdString(request.get_param_value(key)));
    }
    if (request.has_param("limit")) params.insert("limit", QString::fromStdString(request.get_param_value("limit")).toInt());
    resultResponse(response, invoke("comparison.list", params));
  });
  m_server->Get(R"(/api/v1/artifacts/([0-9a-f-]+)/content)", [requireAuth, invoke](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    const QJsonObject result = invoke("artifact.resolve", {{"artifactId", QString::fromStdString(request.matches[1])}});
    if (result.contains("_error")) return sendError(response, 404, "not_found", "Artifact not found");
    const QString path = result.value("absolutePath").toString();
    if (!QFileInfo::exists(path)) return sendError(response, 404, "not_found", "Artifact file is missing");
    response.set_header("Cache-Control", "private, no-store");
    response.set_file_content(path.toStdString());
  });
  m_server->Get(R"(/api/v1/jobs/([0-9a-f-]+)/events)", [this, requireAuth, invoke](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    const QString jobId = QString::fromStdString(request.matches[1]);
    auto stream = streamFor(jobId);
    qint64 lastSequence = -1;
    const std::string lastHeader = request.get_header_value("Last-Event-ID");
    if (!lastHeader.empty()) lastSequence = QString::fromStdString(lastHeader).toLongLong();
    const QJsonObject replay = invoke("job.events", {{"jobId", jobId}, {"afterSequence", lastSequence}});
    {
      std::lock_guard lock(stream->mutex);
      for (const auto &value : replay.value("events").toArray()) {
        const auto event = value.toObject();
        bool duplicate = false;
        for (const auto &known : stream->events) {
          if (known.value("sequence") == event.value("sequence")) { duplicate = true; break; }
        }
        if (!duplicate) stream->events.push_back(event);
        if (isTerminal(event.value("type").toString())) stream->terminal = true;
      }
    }
    struct Cursor { size_t index = 0; };
    auto cursor = std::make_shared<Cursor>();
    {
      std::lock_guard lock(stream->mutex);
      while (cursor->index < stream->events.size() &&
             stream->events[cursor->index].value("sequence").toVariant().toLongLong() <= lastSequence) {
        ++cursor->index;
      }
    }
    response.set_header("Cache-Control", "no-cache");
    response.set_header("X-Accel-Buffering", "no");
    response.set_chunked_content_provider("text/event-stream", [stream, cursor](size_t, httplib::DataSink &sink) {
      QJsonObject event;
      {
        std::unique_lock lock(stream->mutex);
        stream->changed.wait_for(lock, std::chrono::seconds(15), [&] {
          return cursor->index < stream->events.size() || stream->terminal;
        });
        if (cursor->index < stream->events.size()) event = stream->events[cursor->index++];
        else if (stream->terminal) { sink.done(); return false; }
      }
      if (event.isEmpty()) return sink.write(": keepalive\n\n", 13);
      const QByteArray payload = QJsonDocument(event).toJson(QJsonDocument::Compact);
      const QByteArray line = "id: " + QByteArray::number(event.value("sequence").toVariant().toLongLong()) +
                              "\ndata: " + payload + "\n\n";
      return sink.write(line.constData(), line.size());
    });
  });
  m_server->Get("/api/v1/schedules", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QJsonObject params;
    if (request.has_param("projectId")) params.insert("projectId", QString::fromStdString(request.get_param_value("projectId")));
    resultResponse(response, invoke("schedule.list", params));
  });
  m_server->Post("/api/v1/schedules", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    QString parseError;
    const auto body = parseBody(request, &parseError);
    if (!parseError.isEmpty()) return sendError(response, 400, "invalid_json", parseError);
    resultResponse(response, invoke("schedule.upsert", body), 201);
  });
  m_server->Delete(R"(/api/v1/schedules/([0-9a-f-]+))", [requireAuth, invoke, resultResponse](const httplib::Request &request, httplib::Response &response) {
    if (!requireAuth(request, response)) return;
    resultResponse(response, invoke("schedule.remove", {{"scheduleId", QString::fromStdString(request.matches[1])}}));
  });
  m_server->set_error_handler([](const httplib::Request &, httplib::Response &response) {
    if (response.status == 404) sendError(response, 404, "not_found", "API route not found");
  });
}

} // namespace CyberSnapper
