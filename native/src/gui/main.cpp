#include "core/Paths.h"
#include "core/Rpc.h"
#include "gui/MainWindow.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QProcess>
#include <QTabWidget>
#include <QTimer>

using namespace CyberSnapper;

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setOrganizationName("CyberBrand");
  application.setOrganizationDomain("cyberbrand.net");
  application.setApplicationName("CyberSnapper");
  application.setApplicationDisplayName("CyberSnapper");
  application.setApplicationVersion(CYBERSNAPPER_VERSION);
  application.setWindowIcon(QIcon(":/cybersnapper/logo.png"));

  QPalette palette = application.palette();
  for (const auto group : {QPalette::Active, QPalette::Inactive}) {
    const QColor window = palette.color(group, QPalette::Window);
    const QColor button = window.lightness() < 128 ? window.lighter(155) : window.darker(108);
    palette.setColor(group, QPalette::Button, button);
  }
  application.setPalette(palette);
  application.setStyleSheet(R"(
    QPushButton {
      background-color: palette(button);
      color: palette(button-text);
      border: 1px solid palette(highlight);
      border-radius: 5px;
      padding: 6px 12px;
      min-height: 20px;
    }
    QPushButton:hover, QPushButton:focus {
      background-color: palette(midlight);
      border-width: 2px;
      padding: 5px 11px;
    }
    QPushButton:pressed { background-color: palette(mid); }
    QPushButton:disabled {
      color: palette(mid);
      border-color: palette(mid);
    }
    QPushButton#primaryAction {
      background-color: palette(highlight);
      color: palette(highlighted-text);
      font-weight: 600;
      padding-left: 20px;
      padding-right: 20px;
    }
    QPushButton#primaryAction:disabled {
      background-color: palette(button);
      color: palette(mid);
      border-color: palette(mid);
    }
    QGroupBox {
      font-weight: 600;
      margin-top: 8px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      left: 8px;
      padding: 0 4px;
    }
    QToolBar#mainNavigation {
      spacing: 2px;
      padding: 4px;
      border-bottom: 1px solid palette(mid);
    }
    QToolBar#mainNavigation QToolButton {
      padding: 6px 8px;
      margin: 1px;
      border: 1px solid transparent;
      border-radius: 5px;
    }
    QToolBar#mainNavigation QToolButton:hover {
      background-color: palette(midlight);
      border-color: palette(highlight);
    }
    QToolBar#mainNavigation QToolButton:checked {
      background-color: palette(highlight);
      color: palette(highlighted-text);
      font-weight: 600;
    }
    QLabel#helperText {
      background-color: palette(base);
      color: palette(text);
      border-left: 3px solid palette(highlight);
      padding: 5px 7px;
    }
    QLabel#pageTitle {
      font-size: 20px;
      font-weight: 700;
    }
  )");

  QString error;
  if (blockingRpcCall(Paths::agentServerName(), "agent.ping", {}, 300, &error).isEmpty()) {
    QProcess::startDetached(Paths::agentExecutable(), {});
  }

  MainWindow window;
  window.show();
  bool tabOk = false;
  const int requestedTab = qEnvironmentVariableIntValue("CYBERSNAPPER_UI_TAB", &tabOk);
  if (tabOk) {
    if (auto *tabs = window.findChild<QTabWidget *>("mainTabs")) tabs->setCurrentIndex(requestedTab);
  }
  QTimer::singleShot(250, &window, &MainWindow::connectToAgent);
  const QString screenshotPath = qEnvironmentVariable("CYBERSNAPPER_UI_SCREENSHOT");
  if (!screenshotPath.isEmpty()) {
    bool delayOk = false;
    const int requestedDelay = qEnvironmentVariableIntValue("CYBERSNAPPER_UI_SCREENSHOT_DELAY", &delayOk);
    QTimer::singleShot(delayOk ? qMax(250, requestedDelay) : 1500, &window, [&application, &window, screenshotPath] {
      window.grab().save(screenshotPath);
      application.quit();
    });
  }
  return application.exec();
}
