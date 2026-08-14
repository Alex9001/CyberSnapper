#include "core/AgentService.h"
#include "core/Paths.h"
#include "core/Rpc.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTextStream>

using namespace CyberSnapper;

namespace {

int runAgent(QCoreApplication &application, bool withTray) {
  application.setOrganizationName("CyberBrand");
  application.setOrganizationDomain("cyberbrand.net");
  application.setApplicationName("CyberSnapper Agent");
  application.setApplicationVersion(CYBERSNAPPER_VERSION);

  RpcServer rpc;
  AgentService service;
  QString error;
  if (!rpc.listen(Paths::agentServerName(),
                  [&service](const QString &method, const QJsonObject &params) {
                    return service.handle(method, params);
                  }, &error)) {
    QTextStream(stderr) << "CyberSnapper agent is already running or cannot start: " << error << '\n';
    return 2;
  }
  if (!service.start(&error)) {
    QTextStream(stderr) << "CyberSnapper agent could not initialize: " << error << '\n';
    return 1;
  }

  QSystemTrayIcon *tray = nullptr;
  QMenu *menu = nullptr;
  if (withTray && QSystemTrayIcon::isSystemTrayAvailable()) {
    tray = new QSystemTrayIcon(QIcon(":/cybersnapper/logo.png"), &application);
    tray->setToolTip("CyberSnapper Agent");
    menu = new QMenu;
    QAction *open = menu->addAction("Open CyberSnapper");
    QAction *status = menu->addAction("No active jobs");
    status->setEnabled(false);
    menu->addSeparator();
    QAction *quit = menu->addAction("Quit Agent");
    QObject::connect(open, &QAction::triggered, &application, [] {
      QProcess::startDetached(Paths::guiExecutable(), {});
    });
    QObject::connect(quit, &QAction::triggered, &service,
                     [&service] { service.handle("agent.stop", {{"force", true}}); });
    QObject::connect(&service, &AgentService::eventPublished, status,
                     [status](const QString &event, const QJsonObject &data) {
      if (event == "queue.changed") {
        status->setText(QStringLiteral("%1 active, %2 queued")
                            .arg(data.value("active").toInt()).arg(data.value("queued").toInt()));
      }
    });
    QObject::connect(tray, &QSystemTrayIcon::activated, open,
                     [open](QSystemTrayIcon::ActivationReason reason) {
      if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) open->trigger();
    });
    tray->setContextMenu(menu);
    tray->show();
  }

  QObject::connect(&service, &AgentService::eventPublished, &rpc,
                   [&rpc](const QString &event, const QJsonObject &data) { rpc.broadcast(event, data); });
  QObject::connect(&service, &AgentService::notificationRequested, &application,
                   [tray](const QString &title, const QString &message) {
    if (tray) tray->showMessage(title, message, QSystemTrayIcon::Information, 5000);
  });
  QObject::connect(&service, &AgentService::quitRequested, &application, &QCoreApplication::quit);
  QObject::connect(&application, &QCoreApplication::aboutToQuit, &service, &AgentService::shutdown);

  const int code = application.exec();
  delete menu;
  return code;
}

} // namespace

int main(int argc, char **argv) {
  bool headless = false;
  for (int i = 1; i < argc; ++i) if (QString::fromLocal8Bit(argv[i]) == "--headless") headless = true;
  if (headless) {
    QCoreApplication application(argc, argv);
    return runAgent(application, false);
  }
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  return runAgent(application, true);
}
