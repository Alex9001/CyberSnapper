#include "core/BrowserManager.h"

#include "core/Models.h"
#include "core/Paths.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <utility>

namespace CyberSnapper {

namespace {

constexpr int kMaximumLogLines = 200;
constexpr int kMaximumRecentTasks = 50;

QString displayEngine(const QString &engine) {
  if (engine == QStringLiteral("webkit")) return QStringLiteral("WebKit");
  if (engine.isEmpty()) return engine;
  QString display = engine;
  display[0] = display.at(0).toUpper();
  return display;
}

QJsonArray jsonLines(const QStringList &lines) {
  QJsonArray result;
  for (const QString &line : lines) result.append(line);
  return result;
}

} // namespace

BrowserManager::BrowserManager(QObject *parent) : QObject(parent) {
  m_progressTimer.setInterval(1000);
  connect(&m_progressTimer, &QTimer::timeout, this, [this] {
    if (!m_active) return;
    QJsonObject snapshot = m_tasks.value(m_active->id);
    snapshot.insert(QStringLiteral("elapsedMs"), m_elapsed.elapsed());
    m_tasks.insert(m_active->id, snapshot);
    emit progressPublished(snapshot);
  });
}

BrowserManager::~BrowserManager() { shutdown(); }

bool BrowserManager::validEngine(const QString &engine) {
  return QStringList{QStringLiteral("chromium"), QStringLiteral("firefox"),
                     QStringLiteral("webkit")}.contains(engine);
}

QJsonObject BrowserManager::failure(const QString &code, const QString &message, int status) {
  return {{QStringLiteral("_error"), QJsonObject{{QStringLiteral("code"), code},
                                                  {QStringLiteral("message"), message},
                                                  {QStringLiteral("status"), status}}}};
}

QJsonObject BrowserManager::status() {
  const QString worker = Paths::workerEntry();
  if (worker.isEmpty()) return failure(QStringLiteral("worker_missing"),
                                       QStringLiteral("Capture worker is not built"), 503);
  const QString node = Paths::nodeExecutable();
  QProcess process;
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QString cache = Paths::browserCacheDir();
  environment.insert(QStringLiteral("CYBERSNAPPER_BROWSER_CACHE"), cache);
  environment.insert(QStringLiteral("PLAYWRIGHT_BROWSERS_PATH"), cache);
  process.setProcessEnvironment(environment);
  process.start(node, {worker, QStringLiteral("--browsers")});
  if (!process.waitForStarted(5000) || !process.waitForFinished(10000)) {
    return failure(QStringLiteral("browser_status_failed"), process.errorString(), 503);
  }
  QJsonObject result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
  if (process.exitCode() != 0 || result.isEmpty()) {
    QString message = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (message.isEmpty()) message = QStringLiteral("Browser status worker returned no result");
    return failure(QStringLiteral("browser_status_failed"), message, 503);
  }

  QJsonObject browsers = result.value(QStringLiteral("browsers")).toObject();
  for (const QString &engine : {QStringLiteral("chromium"), QStringLiteral("firefox"),
                                QStringLiteral("webkit")}) {
    QJsonObject state = browsers.value(engine).toObject();
    const bool downloaded = state.value(QStringLiteral("downloaded"))
                                .toBool(state.value(QStringLiteral("installed")).toBool());
    state.insert(QStringLiteral("downloaded"), downloaded);
    state.insert(QStringLiteral("installed"), downloaded);
    if (m_engineResults.contains(engine)) {
      const QJsonObject latest = m_engineResults.value(engine);
      for (const QString &key : {QStringLiteral("ready"), QStringLiteral("state"),
                                 QStringLiteral("message"), QStringLiteral("diagnostics"),
                                 QStringLiteral("installId"), QStringLiteral("operation"),
                                 QStringLiteral("logs")}) {
        if (latest.contains(key)) state.insert(key, latest.value(key));
      }
    } else if (!state.contains(QStringLiteral("state"))) {
      state.insert(QStringLiteral("state"), downloaded ? QStringLiteral("downloaded")
                                                       : QStringLiteral("not_downloaded"));
    }
    browsers.insert(engine, state);
  }

  QJsonArray installations;
  if (m_active) installations.append(m_tasks.value(m_active->id));
  for (const Task &queued : std::as_const(m_queue)) installations.append(m_tasks.value(queued.id));
  result.insert(QStringLiteral("browsers"), browsers);
  result.insert(QStringLiteral("installations"), installations);
  result.insert(QStringLiteral("active"), m_active.has_value());
  result.insert(QStringLiteral("queued"), m_queue.size());
  return result;
}

QJsonObject BrowserManager::install(const QString &engine, bool force) {
  return enqueue(engine.trimmed().toLower(), QStringLiteral("install"), force);
}

QJsonObject BrowserManager::verify(const QString &engine) {
  return enqueue(engine.trimmed().toLower(), QStringLiteral("verify"), false);
}

QJsonObject BrowserManager::enqueue(const QString &engine, const QString &operation, bool force) {
  if (!validEngine(engine)) {
    return failure(QStringLiteral("invalid_browser"),
                   QStringLiteral("Browser must be chromium, firefox, or webkit"));
  }
  if (m_shuttingDown) {
    return failure(QStringLiteral("agent_stopping"),
                   QStringLiteral("The browser service is shutting down"), 409);
  }
  if (m_active && m_active->engine == engine) {
    return {{QStringLiteral("accepted"), true}, {QStringLiteral("duplicate"), true},
            {QStringLiteral("installId"), m_active->id},
            {QStringLiteral("engine"), engine},
            {QStringLiteral("state"), m_tasks.value(m_active->id).value(QStringLiteral("state"))}};
  }
  for (const Task &queued : std::as_const(m_queue)) {
    if (queued.engine == engine) {
      return {{QStringLiteral("accepted"), true}, {QStringLiteral("duplicate"), true},
              {QStringLiteral("installId"), queued.id}, {QStringLiteral("engine"), engine},
              {QStringLiteral("state"), QStringLiteral("queued")}};
    }
  }

  Task task{newId(), engine, operation, force};
  QJsonObject snapshot{{QStringLiteral("installId"), task.id},
                       {QStringLiteral("engine"), engine},
                       {QStringLiteral("operation"), operation},
                       {QStringLiteral("state"), QStringLiteral("queued")},
                       {QStringLiteral("phase"), QStringLiteral("queued")},
                       {QStringLiteral("message"),
                        operation == QStringLiteral("verify")
                            ? QStringLiteral("Waiting to check %1…").arg(displayEngine(engine))
                            : QStringLiteral("Waiting to install %1…").arg(displayEngine(engine))}};
  m_tasks.insert(task.id, snapshot);
  m_taskOrder.append(task.id);
  while (m_taskOrder.size() > kMaximumRecentTasks) m_tasks.remove(m_taskOrder.takeFirst());
  m_queue.enqueue(task);
  updateQueuePositions();
  QTimer::singleShot(0, this, &BrowserManager::startNext);
  return {{QStringLiteral("accepted"), true}, {QStringLiteral("installId"), task.id},
          {QStringLiteral("engine"), engine}, {QStringLiteral("state"), QStringLiteral("queued")}};
}

void BrowserManager::updateQueuePositions() {
  for (int index = 0; index < m_queue.size(); ++index) {
    QJsonObject snapshot = m_tasks.value(m_queue.at(index).id);
    snapshot.insert(QStringLiteral("queuePosition"), index + 1);
    m_tasks.insert(m_queue.at(index).id, snapshot);
    emit progressPublished(snapshot);
  }
}

void BrowserManager::startNext() {
  if (m_shuttingDown || m_active || m_queue.isEmpty()) return;
  m_active = m_queue.dequeue();
  updateQueuePositions();
  m_logs.clear();
  m_workerResult = {};
  m_stdoutBuffer.clear();
  m_stderrBuffer.clear();
  m_elapsed.restart();
  m_progressTimer.start();

  const QString worker = Paths::workerEntry();
  if (worker.isEmpty()) {
    finishActive({{QStringLiteral("state"), QStringLiteral("failed")},
                  {QStringLiteral("ok"), false},
                  {QStringLiteral("message"), QStringLiteral("Capture worker is not built")},
                  {QStringLiteral("diagnostics"),
                   QJsonObject{{QStringLiteral("code"), QStringLiteral("worker_missing")},
                               {QStringLiteral("message"), QStringLiteral("Capture worker is not built")}}}});
    return;
  }

  QJsonObject snapshot = m_tasks.value(m_active->id);
  snapshot.remove(QStringLiteral("queuePosition"));
  snapshot.insert(QStringLiteral("state"), m_active->operation == QStringLiteral("verify")
                                                  ? QStringLiteral("verifying")
                                                  : QStringLiteral("preparing"));
  snapshot.insert(QStringLiteral("phase"), snapshot.value(QStringLiteral("state")));
  snapshot.insert(QStringLiteral("message"),
                  m_active->operation == QStringLiteral("verify")
                      ? QStringLiteral("Checking whether %1 can launch…").arg(displayEngine(m_active->engine))
                      : QStringLiteral("Preparing %1 installation…").arg(displayEngine(m_active->engine)));
  m_tasks.insert(m_active->id, snapshot);
  emit progressPublished(snapshot);

  auto *process = new QProcess(this);
  m_process = process;
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QString cache = Paths::browserCacheDir();
  environment.insert(QStringLiteral("CYBERSNAPPER_BROWSER_CACHE"), cache);
  environment.insert(QStringLiteral("PLAYWRIGHT_BROWSERS_PATH"), cache);
  process->setProcessEnvironment(environment);
  process->setProcessChannelMode(QProcess::SeparateChannels);
  connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
    if (m_process == process) consumeOutput(m_stdoutBuffer, process->readAllStandardOutput(), true);
  });
  connect(process, &QProcess::readyReadStandardError, this, [this, process] {
    if (m_process == process) consumeOutput(m_stderrBuffer, process->readAllStandardError(), false);
  });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
    if (m_process != process || error != QProcess::FailedToStart) return;
    finishActive({{QStringLiteral("state"), QStringLiteral("failed")},
                  {QStringLiteral("ok"), false},
                  {QStringLiteral("message"), process->errorString()},
                  {QStringLiteral("diagnostics"),
                   QJsonObject{{QStringLiteral("code"), QStringLiteral("process_start_failed")},
                               {QStringLiteral("message"), process->errorString()}}}});
  });
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, process](int code, QProcess::ExitStatus exitStatus) {
    if (m_process != process) return;
    consumeOutput(m_stdoutBuffer, QByteArrayLiteral("\n"), true);
    consumeOutput(m_stderrBuffer, QByteArrayLiteral("\n"), false);
    QJsonObject result = m_workerResult;
    if (result.isEmpty()) {
      const QString message = m_logs.isEmpty()
          ? QStringLiteral("Browser worker exited without a result") : m_logs.constLast();
      result = {{QStringLiteral("state"), QStringLiteral("failed")},
                {QStringLiteral("ok"), false}, {QStringLiteral("message"), message},
                {QStringLiteral("diagnostics"),
                 QJsonObject{{QStringLiteral("code"),
                              exitStatus == QProcess::CrashExit ? QStringLiteral("worker_crashed")
                                                                : QStringLiteral("worker_failed")},
                             {QStringLiteral("message"), message}}}};
    }
    finishActive(result, code);
  });

  QStringList arguments{worker};
  arguments.append(m_active->operation == QStringLiteral("verify")
                       ? QStringList{QStringLiteral("--verify"), m_active->engine}
                       : QStringList{QStringLiteral("--install"), m_active->engine});
  if (m_active->force) arguments.append(QStringLiteral("--force"));
  process->start(Paths::nodeExecutable(), arguments);
}

void BrowserManager::consumeOutput(QByteArray &buffer, const QByteArray &chunk, bool structured) {
  buffer.append(chunk);
  while (true) {
    const int newline = buffer.indexOf('\n');
    if (newline < 0) break;
    const QByteArray line = buffer.left(newline);
    buffer.remove(0, newline + 1);
    processLine(line, structured);
  }
}

void BrowserManager::processLine(const QByteArray &line, bool structured) {
  const QString text = QString::fromUtf8(line).trimmed();
  if (text.isEmpty() || !m_active) return;
  if (!structured) {
    appendLog(text);
    QJsonObject snapshot = m_tasks.value(m_active->id);
    snapshot.insert(QStringLiteral("message"), text);
    snapshot.insert(QStringLiteral("logs"), jsonLines(m_logs));
    m_tasks.insert(m_active->id, snapshot);
    emit progressPublished(snapshot);
    return;
  }

  QJsonParseError parseError;
  const QJsonObject message = QJsonDocument::fromJson(line, &parseError).object();
  if (parseError.error != QJsonParseError::NoError || message.isEmpty()) {
    appendLog(text);
    return;
  }
  const QString type = message.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("browser_install_result")) {
    m_workerResult = message;
    return;
  }
  if (type != QStringLiteral("browser_install_progress")) return;

  QJsonObject snapshot = m_tasks.value(m_active->id);
  for (auto iterator = message.begin(); iterator != message.end(); ++iterator) {
    if (iterator.key() != QStringLiteral("type") &&
        iterator.key() != QStringLiteral("protocolVersion")) {
      snapshot.insert(iterator.key(), iterator.value());
    }
  }
  const QString phase = snapshot.value(QStringLiteral("phase")).toString();
  snapshot.insert(QStringLiteral("state"), phase == QStringLiteral("verifying")
                                                  ? QStringLiteral("verifying")
                                                  : QStringLiteral("installing"));
  appendLog(snapshot.value(QStringLiteral("message")).toString());
  snapshot.insert(QStringLiteral("logs"), jsonLines(m_logs));
  m_tasks.insert(m_active->id, snapshot);
  emit progressPublished(snapshot);
}

void BrowserManager::appendLog(const QString &line) {
  const QString clean = line.trimmed();
  if (clean.isEmpty()) return;
  m_logs.append(clean);
  while (m_logs.size() > kMaximumLogLines) m_logs.removeFirst();
}

void BrowserManager::finishActive(QJsonObject result, int exitCode) {
  if (!m_active) return;
  const Task task = *m_active;
  QProcess *process = m_process;
  m_process = nullptr;
  m_active.reset();
  m_progressTimer.stop();

  result.remove(QStringLiteral("type"));
  result.remove(QStringLiteral("protocolVersion"));
  result.insert(QStringLiteral("installId"), task.id);
  result.insert(QStringLiteral("engine"), task.engine);
  result.insert(QStringLiteral("operation"), task.operation);
  if (exitCode >= 0) result.insert(QStringLiteral("exitCode"), exitCode);
  if (!m_logs.isEmpty() && !result.contains(QStringLiteral("logs"))) {
    result.insert(QStringLiteral("logs"), jsonLines(m_logs));
  }
  m_tasks.insert(task.id, result);
  m_engineResults.insert(task.engine, result);
  emit finishedPublished(result);
  if (process) process->deleteLater();
  QTimer::singleShot(0, this, &BrowserManager::startNext);
}

void BrowserManager::finishQueued(const Task &task, const QString &state, const QString &message) {
  QJsonObject result{{QStringLiteral("installId"), task.id},
                     {QStringLiteral("engine"), task.engine},
                     {QStringLiteral("operation"), task.operation},
                     {QStringLiteral("state"), state},
                     {QStringLiteral("ok"), false},
                     {QStringLiteral("message"), message}};
  m_tasks.insert(task.id, result);
  emit finishedPublished(result);
}

QJsonObject BrowserManager::cancel(const QString &installId) {
  if (installId.isEmpty()) return failure(QStringLiteral("invalid_install"),
                                          QStringLiteral("An installation ID is required"));
  if (m_active && m_active->id == installId) {
    QJsonObject snapshot = m_tasks.value(installId);
    snapshot.insert(QStringLiteral("state"), QStringLiteral("cancelling"));
    snapshot.insert(QStringLiteral("phase"), QStringLiteral("cancelling"));
    snapshot.insert(QStringLiteral("message"), QStringLiteral("Cancelling browser installation…"));
    m_tasks.insert(installId, snapshot);
    emit progressPublished(snapshot);
    if (m_process) {
      m_process->write(QJsonDocument(QJsonObject{{QStringLiteral("command"),
                                                  QStringLiteral("cancel")}})
                           .toJson(QJsonDocument::Compact) + '\n');
      const QString activeId = installId;
      QTimer::singleShot(5000, this, [this, activeId] {
        if (m_active && m_active->id == activeId && m_process) m_process->kill();
      });
    }
    return {{QStringLiteral("cancelling"), true}, {QStringLiteral("installId"), installId}};
  }
  for (int index = 0; index < m_queue.size(); ++index) {
    if (m_queue.at(index).id != installId) continue;
    const Task task = m_queue.takeAt(index);
    finishQueued(task, QStringLiteral("cancelled"),
                 QStringLiteral("Queued browser installation was cancelled."));
    updateQueuePositions();
    return {{QStringLiteral("cancelled"), true}, {QStringLiteral("installId"), installId}};
  }
  if (m_tasks.contains(installId)) {
    return {{QStringLiteral("cancelled"), false}, {QStringLiteral("installId"), installId},
            {QStringLiteral("state"), m_tasks.value(installId).value(QStringLiteral("state"))}};
  }
  return failure(QStringLiteral("not_found"), QStringLiteral("Browser installation not found"), 404);
}

QJsonObject BrowserManager::cancelAll() {
  QStringList ids;
  if (m_active) ids.append(m_active->id);
  while (!m_queue.isEmpty()) {
    const Task task = m_queue.dequeue();
    ids.append(task.id);
    finishQueued(task, QStringLiteral("cancelled"),
                 QStringLiteral("Queued browser installation was cancelled."));
  }
  if (m_active) cancel(m_active->id);
  return {{QStringLiteral("cancelling"), !ids.isEmpty()},
          {QStringLiteral("installIds"), QJsonArray::fromStringList(ids)}};
}

QJsonObject BrowserManager::task(const QString &installId) const {
  if (!m_tasks.contains(installId)) {
    return failure(QStringLiteral("not_found"), QStringLiteral("Browser installation not found"), 404);
  }
  return {{QStringLiteral("install"), m_tasks.value(installId)}};
}

bool BrowserManager::hasPendingOperations() const { return m_active.has_value() || !m_queue.isEmpty(); }

int BrowserManager::queuedCount() const { return m_queue.size(); }

void BrowserManager::shutdown() {
  if (m_shuttingDown) return;
  m_shuttingDown = true;
  while (!m_queue.isEmpty()) {
    const Task task = m_queue.dequeue();
    finishQueued(task, QStringLiteral("cancelled"),
                 QStringLiteral("Browser installation stopped with the agent."));
  }
  if (!m_process) return;
  m_process->write(QJsonDocument(QJsonObject{{QStringLiteral("command"),
                                              QStringLiteral("cancel")}})
                       .toJson(QJsonDocument::Compact) + '\n');
  if (!m_process->waitForFinished(2000)) {
    m_process->kill();
    m_process->waitForFinished(1000);
  }
}

} // namespace CyberSnapper
