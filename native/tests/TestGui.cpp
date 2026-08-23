#include "gui/MainWindow.h"

#include <QAction>
#include <QComboBox>
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
  void contentBlockingDialog();
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

  auto *presentation = window.findChild<QComboBox *>("presentationScene");
  QVERIFY(presentation);
  QCOMPARE(presentation->currentData().toString(), QString("off"));
  QVERIFY(presentation->findData("aurora") >= 0);
}

#include "gui/ContentBlockingDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>

using namespace CyberSnapper;

void TestGui::contentBlockingDialog() {
  // Default settings for a new project
  const QJsonObject settings = [] {
    QJsonObject obj;
    obj["enabled"] = true;
    obj["consentStrategy"] = "rejectThenDismiss";
    obj["subscriptionIds"] = QJsonArray{"easylist-cookie", "ublock-cookie"};
    obj["customRulesetIds"] = QJsonArray{};
    obj["disabledDomains"] = QJsonArray{};
    obj["versionPolicy"] = "latest";
    return obj;
  }();
  
  // Null RPC: simulates no agent connection (editor disabled)
  ContentBlockingDialog dialog(settings, {}, nullptr);
  
  // Subscription checkboxes: easylist-cookie and ublock-cookie should be checked
  const auto &checks = dialog.findChildren<QCheckBox *>();
  bool easylistFound = false, ublockFound = false;
  for (QCheckBox *check : checks) {
    if (check->text().contains("EasyList Cookie")) {
      easylistFound = true;
      QVERIFY(check->isChecked());
    }
    if (check->text().contains("uBlock Cookie")) {
      ublockFound = true;
      QVERIFY(check->isChecked());
    }
  }
  QVERIFY(easylistFound);
  QVERIFY(ublockFound);
  
  // Strategy combo: rejectThenDismiss (index 0)
  const auto &combos = dialog.findChildren<QComboBox *>();
  QComboBox *strategy = nullptr;
  for (QComboBox *combo : combos) {
    if (combo->count() == 2 && combo->itemData(0).toString() == "rejectThenDismiss") {
      strategy = combo;
      break;
    }
  }
  QVERIFY(strategy);
  QCOMPARE(strategy->currentIndex(), 0);
  
  // Version policy combo: latest (index 0)
  QComboBox *version = nullptr;
  for (QComboBox *combo : combos) {
    if (combo->count() == 2 && combo->itemData(0).toString() == "latest") {
      version = combo;
      break;
    }
  }
  QVERIFY(version);
  QCOMPARE(version->currentIndex(), 0);
  
  // Editor disabled when no RPC
  QTest::qWait(50);
  qDebug() << "Editor widgets:" << dialog.findChildren<QWidget *>();
  QWidget *editor = nullptr;
  for (QWidget *widget : dialog.findChildren<QWidget *>()) {
    QLineEdit *nameEdit = widget->findChild<QLineEdit *>();
    QTextEdit *rulesText = widget->findChild<QTextEdit *>();
    if (nameEdit && rulesText) {
      editor = widget;
      qDebug() << "Found editor:" << editor << "enabled:" << editor->isEnabled();
      break;
    }
  }
  QVERIFY(editor);
  QVERIFY(!editor->isEnabled());
  
  // Validation: add ruleset with empty name → expect rejection
  QPushButton *addButton = nullptr;
  for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
    if (button->text() == "New ruleset") {
      addButton = button;
      break;
    }
  }
  QVERIFY(addButton);
  QTest::mouseClick(addButton, Qt::LeftButton);
  
  // Editor should stay disabled (no RPC)
  QVERIFY(!editor->isEnabled());
}

QTEST_MAIN(TestGui)
#include "TestGui.moc"
