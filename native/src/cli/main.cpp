#include "core/Paths.h"
#include "core/Rpc.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>

using namespace CyberSnapper;

namespace {

std::atomic_bool interrupted = false;

void interruptHandler(int) { interrupted = true; }

void printJson(const QJsonValue &value) {
  const QByteArray bytes = value.isArray()
      ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)
      : QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented);
  QTextStream(stdout) << bytes;
}

bool ensureAgent(QString *error) {
  // Probe into a local string: a cold-start ping is expected to fail, and its
  // "agent not listening" error must not leak into the caller's error slot.
  QString probeError;
  if (!blockingRpcCall(Paths::agentServerName(), "agent.ping", {}, 300, &probeError).isEmpty()) return true;
  if (qEnvironmentVariableIsSet("CYBERSNAPPER_NO_AUTOSTART")) {
    if (error) *error = probeError;
    return false;
  }
  if (!QProcess::startDetached(Paths::agentExecutable(), {"--headless"})) {
    if (error) *error = "Could not launch " + Paths::agentExecutable();
    return false;
  }
  for (int attempt = 0; attempt < 30; ++attempt) {
    QThread::msleep(100);
    QString ignored;
    if (!blockingRpcCall(Paths::agentServerName(), "agent.ping", {}, 300, &ignored).isEmpty()) return true;
  }
  if (error) *error = "Timed out waiting for CyberSnapper agent";
  return false;
}

QJsonObject invoke(const QString &method, const QJsonObject &params, QString *error) {
  return blockingRpcCall(Paths::agentServerName(), method, params, 10000, error);
}

QStringList fileLines(const QString &path, QString *error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) *error = file.errorString();
    return {};
  }
  QStringList result;
  for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
    const QString value = line.trimmed();
    if (!value.isEmpty() && !value.startsWith('#')) result.append(value);
  }
  return result;
}

QJsonArray jsonStrings(const QStringList &values) {
  QJsonArray array;
  for (const auto &value : values) array.append(value);
  return array;
}

void printJobLine(const QJsonObject &job) {
  QTextStream(stdout) << job.value("id").toString() << "  "
                      << job.value("status").toString().leftJustified(11) << "  "
                      << job.value("createdAt").toString() << "  "
                      << job.value("completedArtifacts").toInt() << " completed, "
                      << job.value("failedArtifacts").toInt() << " failed\n";
}

} // namespace

int main(int argc, char **argv) {
  // Keep the version probe independent of Qt initialization. Release packaging
  // uses this path to verify that the installed executable and its loader-level
  // dependencies can start on every supported platform.
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("cybersnapper-cli %s\n", CYBERSNAPPER_VERSION);
    return 0;
  }

  QCoreApplication application(argc, argv);
  application.setOrganizationName("CyberBrand");
  application.setOrganizationDomain("cyberbrand.net");
  application.setApplicationName("cybersnapper-cli");
  application.setApplicationVersion(CYBERSNAPPER_VERSION);

  QCommandLineParser parser;
  parser.setApplicationDescription("Native CyberSnapper capture automation CLI");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("command", "capture, jobs, job, projects, targets, review, schedules, api, or agent");
  parser.addPositionalArgument("arguments", "Command arguments", "[arguments…]");
  QCommandLineOption jsonOption("json", "Print machine-readable JSON.");
  QCommandLineOption projectOption({"p", "project"}, "Project ID.", "id");
  QCommandLineOption fileOption({"f", "file"}, "Read capture URLs from a text file.", "path");
  QCommandLineOption profileOption("profile", "Capture profile ID.", "id", "default");
  QCommandLineOption targetSetOption("target-set", "Capture a saved target set instead of positional URLs.", "id");
  QCommandLineOption engineOption("engine", "Browser engine; repeat for multiple engines.", "engine");
  QCommandLineOption formatOption("format", "Output format; repeat for multiple formats.", "format");
  QCommandLineOption modeOption("mode", "Capture mode: fullPage, viewport, or element.", "mode");
  QCommandLineOption selectorOption("selector", "CSS selector for element capture.", "selector");
  QCommandLineOption noWaitOption("no-wait", "Queue a capture without waiting for completion.");
  QCommandLineOption forceOption("force", "Force an agent stop while jobs are active.");
  QCommandLineOption limitOption("limit", "Maximum jobs to list.", "count", "50");
  parser.addOptions({jsonOption, projectOption, fileOption, profileOption, targetSetOption, engineOption, formatOption,
                     modeOption, selectorOption, noWaitOption, forceOption, limitOption});
  parser.process(application);

  const QStringList positionals = parser.positionalArguments();
  if (positionals.isEmpty()) parser.showHelp(2);
  const QString command = positionals.first().toLower();
  const QStringList arguments = positionals.mid(1);
  const bool json = parser.isSet(jsonOption);
  QString error;
  if (!ensureAgent(&error)) {
    if (json) printJson(QJsonObject{{"error", error}});
    else QTextStream(stderr) << "cybersnapper-cli: " << error << '\n';
    return 1;
  }

  QJsonObject result;
  if (command == "capture") {
    QStringList urls = arguments;
    if (parser.isSet(fileOption)) urls.append(fileLines(parser.value(fileOption), &error));
    if (!error.isEmpty() || (urls.isEmpty() && !parser.isSet(targetSetOption))) {
      QTextStream(stderr) << "cybersnapper-cli: " << (error.isEmpty() ? "provide URLs or --target-set" : error) << '\n';
      return 2;
    }
    QJsonObject params{{"projectId", parser.value(projectOption)}, {"profileId", parser.value(profileOption)},
                       {"urls", jsonStrings(urls)}, {"targetSetId", parser.value(targetSetOption)}, {"source", "cli"}};
    QJsonObject profile;
    if (parser.isSet(engineOption)) profile.insert("engines", jsonStrings(parser.values(engineOption)));
    if (parser.isSet(formatOption)) profile.insert("formats", jsonStrings(parser.values(formatOption)));
    if (parser.isSet(modeOption)) profile.insert("captureMode", parser.value(modeOption));
    if (parser.isSet(selectorOption)) profile.insert("elementSelector", parser.value(selectorOption));
    if (!profile.isEmpty()) {
      QJsonObject fetched = invoke("profile.get", {{"projectId", parser.value(projectOption)},
                                                    {"profileId", parser.value(profileOption)}}, &error)
                                .value("profile").toObject();
      for (auto it = profile.begin(); it != profile.end(); ++it) fetched.insert(it.key(), it.value());
      params.insert("profile", fetched);
    }
    result = invoke("job.submit", params, &error);
    if (error.isEmpty() && !parser.isSet(noWaitOption)) {
      const QString jobId = result.value("jobId").toString();
      if (!json) QTextStream(stdout) << "Queued " << jobId << '\n';
      std::signal(SIGINT, interruptHandler);
      QString previousStatus;
      bool cancelSent = false;
      while (error.isEmpty()) {
        const QJsonObject response = invoke("job.get", {{"jobId", jobId}}, &error);
        const QJsonObject job = response.value("job").toObject();
        const QString status = job.value("status").toString();
        if (!json && status != previousStatus) {
          QTextStream(stdout) << status << " — " << job.value("completedArtifacts").toInt()
                              << " completed, " << job.value("failedArtifacts").toInt() << " failed\n";
          previousStatus = status;
        }
        if (QStringList{"succeeded", "partial", "failed", "cancelled", "interrupted"}.contains(status)) {
          result = response;
          break;
        }
        if (interrupted && !cancelSent) {
          QString cancelError;
          invoke("job.cancel", {{"jobId", jobId}}, &cancelError);
          cancelSent = true;
        }
        QThread::msleep(500);
      }
    }
  } else if (command == "jobs") {
    result = invoke("job.list", {{"projectId", parser.value(projectOption)},
                                  {"limit", parser.value(limitOption).toInt()}}, &error);
  } else if (command == "job") {
    if (arguments.size() < 2) {
      QTextStream(stderr) << "Usage: cybersnapper-cli job <show|cancel|retry> <job-id>\n";
      return 2;
    }
    const QString action = arguments.at(0);
    const QString method = action == "show" ? "job.get" : action == "cancel" ? "job.cancel" :
                           action == "retry" ? "job.retry" : QString{};
    if (method.isEmpty()) { QTextStream(stderr) << "Unknown job action: " << action << '\n'; return 2; }
    result = invoke(method, {{"jobId", arguments.at(1)}}, &error);
  } else if (command == "projects") {
    if (arguments.isEmpty() || arguments.first() == "list") result = invoke("project.list", {}, &error);
    else if (arguments.first() == "open" && arguments.size() >= 2) result = invoke("project.open", {{"root", arguments.at(1)}}, &error);
    else if (arguments.first() == "create" && arguments.size() >= 2) {
      result = invoke("project.create", {{"root", arguments.at(1)}, {"name", arguments.value(2)}}, &error);
    } else { QTextStream(stderr) << "Usage: cybersnapper-cli projects [list|open <folder>|create <folder> [name]]\n"; return 2; }
  } else if (command == "targets") {
    const QString action = arguments.value(0, "list");
    if (action == "list") result = invoke("targetSet.list", {{"projectId", parser.value(projectOption)}}, &error);
    else if (action == "show" && arguments.size() >= 2) result = invoke("targetSet.get", {{"projectId", parser.value(projectOption)}, {"targetSetId", arguments.at(1)}}, &error);
    else { QTextStream(stderr) << "Usage: cybersnapper-cli targets [list|show <target-set-id>]\n"; return 2; }
  } else if (command == "review") {
    const QString action = arguments.value(0, "list");
    if (action == "list") result = invoke("comparison.list", {{"projectId", parser.value(projectOption)}}, &error);
    else if (QStringList{"accept", "ignore", "reset"}.contains(action) && arguments.size() >= 2) {
      const QString status = action == "accept" ? "accepted" : action == "ignore" ? "ignored" : "unreviewed";
      result = invoke("comparison.review.set", {{"projectId", parser.value(projectOption)},
                                                  {"comparisonId", arguments.at(1)}, {"status", status}}, &error);
    } else { QTextStream(stderr) << "Usage: cybersnapper-cli review [list|accept|ignore|reset <comparison-id>]\n"; return 2; }
  } else if (command == "schedules") {
    if (arguments.isEmpty() || arguments.first() == "list") {
      result = invoke("schedule.list", {{"projectId", parser.value(projectOption)}}, &error);
    } else if (arguments.first() == "run" && arguments.size() >= 2) {
      result = invoke("schedule.runNow", {{"projectId", parser.value(projectOption)}, {"scheduleId", arguments.at(1)}}, &error);
    } else { QTextStream(stderr) << "Usage: cybersnapper-cli schedules [list|run <schedule-id>]\n"; return 2; }
  } else if (command == "api") {
    const QString action = arguments.value(0, "status");
    if (action == "status") result = invoke("api.status", {}, &error);
    else if (action == "enable" || action == "disable") result = invoke("api.setEnabled", {{"enabled", action == "enable"}}, &error);
    else if (action == "token") result = invoke("api.regenerateToken", {}, &error);
    else { QTextStream(stderr) << "Usage: cybersnapper-cli api [status|enable|disable|token]\n"; return 2; }
  } else if (command == "agent") {
    const QString action = arguments.value(0, "status");
    if (action == "status") result = invoke("agent.status", {}, &error);
    else if (action == "stop") result = invoke("agent.stop", {{"force", parser.isSet(forceOption)}}, &error);
    else { QTextStream(stderr) << "Usage: cybersnapper-cli agent [status|stop]\n"; return 2; }
  } else {
    QTextStream(stderr) << "Unknown command: " << command << '\n';
    return 2;
  }

  if (!error.isEmpty()) {
    if (json) printJson(QJsonObject{{"error", error}});
    else QTextStream(stderr) << "cybersnapper-cli: " << error << '\n';
    return 1;
  }
  if (json) {
    printJson(result);
  } else if (command == "jobs") {
    for (const auto &value : result.value("jobs").toArray()) printJobLine(value.toObject());
  } else {
    printJson(result);
  }
  if (command == "capture") {
    const QString status = result.value("job").toObject().value("status").toString(result.value("status").toString());
    return QStringList{"failed", "partial", "cancelled", "interrupted"}.contains(status) ? 1 : 0;
  }
  return 0;
}
