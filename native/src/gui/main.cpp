#include "core/Paths.h"
#include "core/Rpc.h"
#include "gui/MainWindow.h"
#include "gui/Appearance.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QTextStream>
#include <QVersionNumber>

using namespace CyberSnapper;

namespace {

bool newerApplicationVersion(const QString &agentVersion) {
  const QVersionNumber application = QVersionNumber::fromString(QCoreApplication::applicationVersion());
  const QVersionNumber agent = QVersionNumber::fromString(agentVersion);
  return !application.isNull() && !agent.isNull() && QVersionNumber::compare(application, agent) > 0;
}

void ensureCurrentAgent() {
  const QString server = Paths::agentServerName();
  QString error;
  const QJsonObject ping = blockingRpcCall(server, QStringLiteral("agent.ping"), {}, 300, &error);
  if (ping.isEmpty()) {
    QProcess::startDetached(Paths::agentExecutable(), {});
    return;
  }

  const QString agentVersion = ping.value(QStringLiteral("version")).toString();
  if (agentVersion == QCoreApplication::applicationVersion() || !newerApplicationVersion(agentVersion)) return;

  error.clear();
  const QJsonObject status = blockingRpcCall(server, QStringLiteral("agent.status"), {}, 1000, &error);
  if (status.isEmpty() || status.value(QStringLiteral("activeJobs")).toInt() > 0 ||
      status.value(QStringLiteral("queuedJobs")).toInt() > 0 ||
      status.value(QStringLiteral("browserOperations")).toBool() ||
      status.value(QStringLiteral("queuedBrowserOperations")).toInt() > 0) {
    QTextStream(stderr) << "CyberSnapper " << QCoreApplication::applicationVersion()
                        << " found agent " << agentVersion
                        << "; the older agent is busy and was left running.\n";
    return;
  }

  error.clear();
  if (blockingRpcCall(server, QStringLiteral("agent.stop"), {{QStringLiteral("force"), false}},
                      1000, &error).isEmpty()) {
    QTextStream(stderr) << "Could not stop older CyberSnapper agent " << agentVersion
                        << ": " << error << '\n';
    return;
  }
  for (int attempt = 0; attempt < 30; ++attempt) {
    QThread::msleep(100);
    QString ignored;
    if (blockingRpcCall(server, QStringLiteral("agent.ping"), {}, 100, &ignored).isEmpty()) {
      QProcess::startDetached(Paths::agentExecutable(), {});
      return;
    }
  }
  QTextStream(stderr) << "Older CyberSnapper agent did not stop in time; it was not replaced.\n";
}

void selectRequestedTab(MainWindow &window) {
  bool tabOk = false;
  const int requestedTab = qEnvironmentVariableIntValue("CYBERSNAPPER_UI_TAB", &tabOk);
  if (tabOk) {
    if (auto *tabs = window.findChild<QTabWidget *>("mainTabs")) tabs->setCurrentIndex(requestedTab);
  }
}

} // namespace

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setOrganizationName("CyberBrand");
  application.setOrganizationDomain("cyberbrand.net");
  application.setApplicationName("CyberSnapper");
  application.setApplicationDisplayName("CyberSnapper");
  application.setApplicationVersion(CYBERSNAPPER_VERSION);
  application.setWindowIcon(QIcon(":/cybersnapper/logo.png"));

  Appearance::apply(application,
                    qEnvironmentVariable("CYBERSNAPPER_UI_THEME").compare("dark", Qt::CaseInsensitive) == 0);

  ensureCurrentAgent();

  MainWindow window;
  const QString screenshotPath = qEnvironmentVariable("CYBERSNAPPER_UI_SCREENSHOT");
  if (!screenshotPath.isEmpty()) window.resize(1280, 800);
  window.show();
  selectRequestedTab(window);
  QTimer::singleShot(250, &window, &MainWindow::connectToAgent);
  if (!screenshotPath.isEmpty()) {
    const QString scene = qEnvironmentVariable("CYBERSNAPPER_UI_SCENE").trimmed().toLower();
    if (!scene.isEmpty()) {
      auto *poll = new QTimer(&window);
      poll->setInterval(100);
      auto *elapsed = new int(0);
      QObject::connect(poll, &QTimer::timeout, &window,
                       [&application, &window, screenshotPath, scene, poll, elapsed] {
        *elapsed += poll->interval();
        if (!window.prepareScreenshotScene(scene)) {
          if (*elapsed < 15000) return;
          QTextStream(stderr) << "Timed out preparing documentation scene: " << scene << '\n';
          poll->stop();
          delete elapsed;
          application.exit(3);
          return;
        }
        poll->stop();
        QTimer::singleShot(200, &window, [&application, &window, screenshotPath, elapsed] {
          QDir().mkpath(QFileInfo(screenshotPath).absolutePath());
          // Modal-targeting scenes (presentation, pagePreparation,
          // contentBlocking) grab the open dialog; everything else grabs the
          // main window.
          QWidget *target = QApplication::activeModalWidget();
          if (!target) target = &window;
          const bool saved = target->grab().save(screenshotPath, "PNG");
          delete elapsed;
          application.exit(saved ? 0 : 4);
        });
      });
      poll->start();
      return application.exec();
    }
    bool delayOk = false;
    const int requestedDelay = qEnvironmentVariableIntValue("CYBERSNAPPER_UI_SCREENSHOT_DELAY", &delayOk);
    QTimer::singleShot(delayOk ? qMax(250, requestedDelay) : 1500, &window, [&application, &window, screenshotPath] {
      window.grab().save(screenshotPath);
      application.quit();
    });
  }
  return application.exec();
}
