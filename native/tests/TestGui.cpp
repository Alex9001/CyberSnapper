#include "gui/MainWindow.h"

#include <QAction>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QToolBar>

using namespace CyberSnapper;

class TestGui final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
  void primaryNavigationAndWorkspaces();
};

void TestGui::primaryNavigationAndWorkspaces() {
  MainWindow window;
  auto *tabs = window.findChild<QTabWidget *>("mainTabs");
  QVERIFY(tabs);
  QCOMPARE(tabs->count(), 6);
  QCOMPARE(tabs->tabText(0), QString("Capture"));
  QCOMPARE(tabs->tabText(2), QString("Compare"));
  QCOMPARE(tabs->tabText(5), QString("Help"));

  auto *toolbar = window.findChild<QToolBar *>("mainNavigation");
  QVERIFY(toolbar);
  QStringList actions;
  for (QAction *action : toolbar->actions()) actions.append(action->text());
  QVERIFY(actions.contains("Capture"));
  QVERIFY(actions.contains("Help"));
  QVERIFY(actions.contains("About"));

  auto *compare = window.findChild<QSplitter *>("compareSplit");
  QVERIFY(compare);
  QCOMPARE(compare->count(), 2);
  QVERIFY(compare->widget(0)->minimumWidth() >= 430);

  bool startFound = false;
  bool managerFound = false;
  for (QPushButton *button : window.findChildren<QPushButton *>()) {
    startFound = startFound || button->text() == "Start Capture";
    managerFound = managerFound || button->text() == "Manage…";
  }
  QVERIFY(startFound);
  QVERIFY(managerFound);
}

QTEST_MAIN(TestGui)
#include "TestGui.moc"
