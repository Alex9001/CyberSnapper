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
  QCOMPARE(tabs->count(), 8);
  QCOMPARE(tabs->tabText(0), QString("Dashboard"));
  QCOMPARE(tabs->tabText(2), QString("Review"));
  QCOMPARE(tabs->tabText(4), QString("Targets"));
  QCOMPARE(tabs->tabText(7), QString("Help"));
  QCOMPARE(tabs->currentIndex(), 1);

  auto *toolbar = window.findChild<QToolBar *>("mainNavigation");
  QVERIFY(toolbar);
  QStringList actions;
  for (QAction *action : toolbar->actions()) actions.append(action->text());
  QVERIFY(actions.contains("Capture"));
  QVERIFY(actions.contains("Review"));
  QVERIFY(actions.contains("Targets"));

  auto *compare = window.findChild<QSplitter *>("compareSplit");
  QVERIFY(compare);
  QCOMPARE(compare->count(), 2);
  QVERIFY(compare->widget(0)->minimumWidth() < 430);
  QCOMPARE(window.minimumWidth(), 760);

  bool startFound = false;
  bool managerFound = false;
  bool targetSetFound = false;
  bool acceptFound = false;
  for (QPushButton *button : window.findChildren<QPushButton *>()) {
    startFound = startFound || button->text() == "Start Capture";
    managerFound = managerFound || button->text() == "Manage…";
    targetSetFound = targetSetFound || button->text() == "New Target Set";
    acceptFound = acceptFound || button->text() == "Accept & Update Baseline";
  }
  QVERIFY(startFound);
  QVERIFY(managerFound);
  QVERIFY(targetSetFound);
  QVERIFY(acceptFound);
}

QTEST_MAIN(TestGui)
#include "TestGui.moc"
