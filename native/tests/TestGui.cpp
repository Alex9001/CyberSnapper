#include "gui/MainWindow.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
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
  void captureThemeAndProgress();
  void contentBlockingDialog();
};

void TestGui::captureThemeAndProgress() {
  MainWindow window;
  auto *theme = window.findChild<QComboBox *>(QStringLiteral("captureColorScheme"));
  auto *folder = window.findChild<QPushButton *>(QStringLiteral("openCaptureOutput"));
  auto *progress = window.findChild<QProgressBar *>(QStringLiteral("captureProgress"));
  auto *status = window.findChild<QLabel *>(QStringLiteral("captureStatus"));
  QVERIFY(theme && folder && progress && status);
  QCOMPARE(theme->currentData().toString(), QStringLiteral("light"));
  QCOMPARE(theme->count(), 3);
  theme->setCurrentIndex(theme->findData(QStringLiteral("both")));
  QCOMPARE(theme->currentData().toString(), QStringLiteral("both"));
  auto *rpc = window.findChild<RpcClient *>();
  QVERIFY(rpc);
  const auto send = [rpc](QJsonObject event) {
    event.insert("jobId", QStringLiteral("test-job"));
    emit rpc->eventReceived(QStringLiteral("job.event"), event);
  };
  send({{"type", "job_started"}, {"status", "running"}, {"totalArtifacts", 4}, {"sequence", 1}});
  send({{"type", "target_progress"}, {"position", 1}, {"totalTargets", 2}, {"url", "https://example.com/sample"},
        {"viewportName", "Desktop"}, {"engine", "chromium"}, {"colorScheme", "light"},
        {"stage", "Loading page"}, {"elapsedSeconds", 2}, {"sequence", 2}});
  send({{"type", "target_progress"}, {"position", 2}, {"totalTargets", 2}, {"url", "https://example.com/sample"},
        {"viewportName", "Desktop"}, {"engine", "chromium"}, {"colorScheme", "dark"},
        {"stage", "Taking screenshot"}, {"elapsedSeconds", 0}, {"sequence", 3}});
  QVERIFY(status->text().contains(QStringLiteral("1/2")));
  QVERIFY(status->toolTip().contains(QStringLiteral("2/2")));
  QVERIFY(status->text().contains(QStringLiteral("Loading page (2 s)")));
  QVERIFY(status->toolTip().contains(QStringLiteral("dark")));
  send({{"type", "job_progress"}, {"completed", 2}, {"failed", 1}, {"totalArtifacts", 4}, {"sequence", 4}});
  QCOMPARE(progress->maximum(), 4);
  QCOMPARE(progress->value(), 3);
  send({{"type", "job_progress"}, {"completed", 0}, {"sequence", 2}});
  QCOMPARE(progress->value(), 3); // Replayed older events cannot regress the UI.
  send({{"type", "job_partial"}, {"status", "partial"}, {"completed", 3}, {"failed", 1}, {"sequence", 5}});
  QCOMPARE(progress->value(), 4);
  QVERIFY(status->text().contains(QStringLiteral("partial")));
  QVERIFY(!status->text().contains(QStringLiteral("Loading page")));
  send({{"type", "job_cancelled"}, {"message", "Cancelled before start"}, {"sequence", 6}});
  QVERIFY(status->text().contains(QStringLiteral("cancelled")));
  QVERIFY(status->text().contains(QStringLiteral("Cancelled before start")));
}

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

  for (const QString &engine : {QStringLiteral("chromium"), QStringLiteral("firefox"),
                                QStringLiteral("webkit")}) {
    QVERIFY(window.findChild<QLabel *>("browserStatus_" + engine));
    QVERIFY(window.findChild<QPushButton *>("browserInstall_" + engine));
    QVERIFY(window.findChild<QPushButton *>("browserVerify_" + engine));
    QVERIFY(window.findChild<QPushButton *>("browserCancel_" + engine));
    QVERIFY(window.findChild<QProgressBar *>("browserProgress_" + engine));
    QVERIFY(window.findChild<QPlainTextEdit *>("browserLog_" + engine));
  }
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

  // Editor disabled when no RPC
  QTest::qWait(50);
  QWidget *editor = nullptr;
  for (QWidget *widget : dialog.findChildren<QWidget *>()) {
    QLineEdit *nameEdit = widget->findChild<QLineEdit *>();
    QTextEdit *rulesText = widget->findChild<QTextEdit *>();
    if (nameEdit && rulesText) {
      editor = widget;
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

  // A selected custom ruleset must survive opening and accepting the dialog.
  QJsonObject customSettings = settings;
  customSettings.insert("customRulesetIds", QJsonArray{"ruleset-one"});
  const ContentBlockingDialog::RpcInvoker rpc = [](
      const QString &method, const QJsonObject &,
      std::function<void(const QJsonObject &)> success,
      std::function<void(const QString &)>) {
    if (method == "contentBlocking.status") {
      success({{"subscriptions", QJsonArray{}}});
    } else if (method == "contentRuleset.list") {
      success({{"contentRulesets", QJsonArray{QJsonObject{{"id", "ruleset-one"},
          {"name", "Portfolio cleanup"}, {"kind", "custom"},
          {"rulesText", "example.org##.banner"}, {"actions", QJsonArray{}}}}}});
    }
  };
  ContentBlockingDialog customDialog(customSettings, rpc, nullptr);
  auto *rulesets = customDialog.findChild<QListWidget *>("contentRulesetList");
  QVERIFY(rulesets);
  QCOMPARE(rulesets->count(), 1);
  QCOMPARE(rulesets->item(0)->checkState(), Qt::Checked);
  QCOMPARE(customDialog.settings().value("customRulesetIds").toArray(),
           QJsonArray{"ruleset-one"});
}

QTEST_MAIN(TestGui)
#include "TestGui.moc"
