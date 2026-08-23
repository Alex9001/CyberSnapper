#include "gui/ContentBlockingDialog.h"

#include "core/ContentRulesets.h"
#include "core/Models.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <memory>

namespace CyberSnapper {

namespace {

QString joinedDomains(const QJsonValue &value) {
  QStringList domains;
  for (const auto &domain : value.toArray()) domains.append(domain.toString());
  return domains.join(", ");
}

} // namespace

ContentBlockingDialog::ContentBlockingDialog(const QJsonObject &settings, const RpcInvoker &rpc,
                                             QWidget *parent)
    : QDialog(parent), m_rpc(rpc), m_enabled(settings.value("enabled").toBool(true)) {
  for (const auto &value : settings.value("customRulesetIds").toArray()) {
    const QString id = value.toString().trimmed();
    if (!id.isEmpty()) m_selectedRulesetIds.append(id);
  }
  setWindowTitle("Configure Content Blocking");
  resize(780, 620);
  auto *layout = new QVBoxLayout(this);

  auto *listsGroup = new QGroupBox("Filter lists", this);
  auto *listsForm = new QGridLayout(listsGroup);
  const QList<QPair<QString, QStringList>> sections{
      {"Cookies", {"easylist-cookie", "ublock-cookie"}},
      {"Ads", {"easylist-ads"}},
      {"Trackers", {"easyprivacy"}},
      {"Other annoyances", {"ublock-annoyances"}},
  };
  QSet<QString> selected;
  for (const auto &id : settings.value("subscriptionIds").toArray()) selected.insert(id.toString());
  int row = 0;
  for (const auto &section : sections) {
    auto *heading = new QLabel(section.first, listsGroup);
    heading->setStyleSheet("font-weight: 600;");
    listsForm->addWidget(heading, row, 0, 1, 2);
    ++row;
    for (const auto &info : subscriptionCatalog()) {
      if (!section.second.contains(info.id)) continue;
      auto *check = new QCheckBox(listsGroup);
      check->setChecked(selected.contains(info.id));
      check->setToolTip(QStringLiteral("%1\nLicense: %2\nSource: %3")
                            .arg(info.name, info.license, info.sourceUrl));
      m_subscriptionChecks.append(check);
      m_subscriptionIds.append(info.id);
      listsForm->addWidget(check, row, 0, 1, 2);
      ++row;
    }
  }
  layout->addWidget(listsGroup);
  refreshSubscriptionStatus();

  auto *behavior = new QGroupBox("Consent handling", this);
  auto *behaviorForm = new QFormLayout(behavior);
  m_consentStrategy = new QComboBox(behavior);
  m_consentStrategy->addItem("Prefer rejecting non-essential cookies, then dismiss",
                             "rejectThenDismiss");
  m_consentStrategy->addItem("Dismiss banners using the site's own accept control", "dismiss");
  m_consentStrategy->setCurrentIndex(qMax(0, m_consentStrategy->findData(
      settings.value("consentStrategy").toString("rejectThenDismiss"))));
  m_disabledDomains = new QLineEdit(joinedDomains(settings.value("disabledDomains")), behavior);
  m_disabledDomains->setPlaceholderText("mail.example.com, app.example.com");
  behaviorForm->addRow("Strategy", m_consentStrategy);
  behaviorForm->addRow("Never block on sites", m_disabledDomains);
  behaviorForm->addRow(new QLabel(
      "Content blocking is skipped entirely on the listed sites.", behavior));
  layout->addWidget(behavior);

  auto *rulesetsGroup = new QGroupBox("Custom rulesets", this);
  auto *rulesetLayout = new QHBoxLayout(rulesetsGroup);
  auto *left = new QVBoxLayout;
  m_rulesetList = new QListWidget(rulesetsGroup);
  m_rulesetList->setObjectName("contentRulesetList");
  m_rulesetList->setMinimumWidth(190);
  left->addWidget(m_rulesetList, 1);
  auto *addButton = new QPushButton("New ruleset", rulesetsGroup);
  left->addWidget(addButton);
  rulesetLayout->addLayout(left, 1);

  m_editor = new QWidget(rulesetsGroup);
  auto *editorForm = new QFormLayout(m_editor);
  m_rulesetName = new QLineEdit(m_editor);
  m_rulesText = new QTextEdit(m_editor);
  m_rulesText->setFontFamily("monospace");
  m_rulesText->setPlaceholderText(
      "EasyList-style filters, one per line.\n"
      "Network: ||ads.example.com^\n"
      "Cosmetic: example.org##.cookie-banner\n"
      "Exception: @@||cdn.example.com^\n"
      "Comments start with !");
  m_actions = new QTableWidget(0, 4, m_editor);
  m_actions->setHorizontalHeaderLabels({"On site", "Selector", "Action", "Delay (ms)"});
  m_actions->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_actions->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_actions->setMaximumHeight(150);
  editorForm->addRow("Name", m_rulesetName);
  editorForm->addRow("Filters", m_rulesText);
  editorForm->addRow("Structured actions", m_actions);
  auto *actionButtons = new QHBoxLayout;
  auto *addAction = new QPushButton("Add action", m_editor);
  auto *removeAction = new QPushButton("Remove last", m_editor);
  actionButtons->addWidget(addAction);
  actionButtons->addWidget(removeAction);
  actionButtons->addStretch();
  editorForm->addRow(QString(), actionButtons);
  rulesetLayout->addWidget(m_editor, 2);

  m_error = new QLabel(rulesetsGroup);
  m_error->setStyleSheet("color: #b3261e;");
  m_error->setWordWrap(true);
  m_error->hide();
  layout->addWidget(rulesetsGroup, 1);
  layout->addWidget(m_error);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] { acceptDialog(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(addButton, &QPushButton::clicked, this, [this] {
    const int newRow = m_rulesetList->count();
    auto *item = new QListWidgetItem(QStringLiteral("Untitled ruleset"));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    item->setData(Qt::UserRole, newId());
    item->setData(Qt::UserRole + 1, QJsonObject{{"name", ""}, {"kind", "custom"},
                                                {"rulesText", ""}, {"actions", QJsonArray{}}});
    m_rulesetList->addItem(item);
    m_rulesetList->setCurrentRow(newRow);
    showRulesetRow(newRow);
    m_rulesetName->setFocus();
  });
  connect(removeAction, &QPushButton::clicked, this, [this] {
    if (m_actions->rowCount() > 0) m_actions->removeRow(m_actions->rowCount() - 1);
    markDirty();
  });
  connect(addAction, &QPushButton::clicked, this, [this] {
    const int newRow = m_actions->rowCount();
    m_actions->insertRow(newRow);
    m_actions->setItem(newRow, 0, new QTableWidgetItem(""));
    m_actions->setItem(newRow, 1, new QTableWidgetItem(""));
    auto *actionCombo = new QComboBox();
    actionCombo->addItem("Hide", "hide");
    actionCombo->addItem("Click", "click");
    connect(actionCombo, &QComboBox::currentIndexChanged, this, [this] { markDirty(); });
    m_actions->setCellWidget(newRow, 2, actionCombo);
    m_actions->setItem(newRow, 3, new QTableWidgetItem("0"));
    markDirty();
  });
  connect(m_rulesetList, &QListWidget::currentRowChanged, this,
          [this](int currentRow) { showRulesetRow(currentRow); });
  connect(m_rulesetName, &QLineEdit::textChanged, this, [this] {
    if (!m_filling) markDirty();
  });
  connect(m_rulesText, &QTextEdit::textChanged, this, [this] {
    if (!m_filling) markDirty();
  });
  connect(m_actions, &QTableWidget::cellChanged, this, [this](int, int) {
    if (!m_filling) markDirty();
  });

  loadRulesets();
}

void ContentBlockingDialog::refreshSubscriptionStatus() {
  const auto findInfo = [this](const QString &id) -> SubscriptionInfo {
    for (const auto &info : subscriptionCatalog()) {
      if (info.id == id) return info;
    }
    return {};
  };
  // Immediate baseline from the local cache file.
  for (int index = 0; index < m_subscriptionChecks.size(); ++index) {
    const SubscriptionInfo info = findInfo(m_subscriptionIds.at(index));
    const QFileInfo cached(ContentRulesets::cachedListPath(info.id));
    m_subscriptionChecks.at(index)->setText(
        info.name + (cached.exists() ? QStringLiteral("  \u00b7  list downloaded")
                                     : QStringLiteral("  \u00b7  not downloaded yet")));
  }
  if (!m_rpc) return;
  m_rpc("contentBlocking.status", {}, [this, findInfo](const QJsonObject &result) {
    for (const auto &value : result.value("subscriptions").toArray()) {
      const QJsonObject status = value.toObject();
      const int index = m_subscriptionIds.indexOf(status.value("id").toString());
      if (index < 0) continue;
      if (!status.value("downloaded").toBool()) continue; // keep baseline label
      const SubscriptionInfo info = findInfo(status.value("id").toString());
      QString detail = QStringLiteral("%1 rule(s)").arg(status.value("ruleCount").toInt());
      const QDateTime fetched =
          QDateTime::fromString(status.value("fetchedAt").toString(), Qt::ISODate);
      if (fetched.isValid()) {
        detail += QStringLiteral(" \u00b7 updated %1")
                      .arg(fetched.date().toString(Qt::ISODate));
      }
      m_subscriptionChecks.at(index)->setText(info.name + QStringLiteral("  \u00b7  ") + detail);
    }
  }, [](const QString &) {});
}

void ContentBlockingDialog::loadRulesets() {
  if (!m_rpc) {
    m_editor->setEnabled(false);
    m_editor->parentWidget()->setEnabled(false);
    return;
  }
  m_rpc("contentRuleset.list", {}, [this](const QJsonObject &result) {
    for (const auto &value : result.value("contentRulesets").toArray()) {
      const QJsonObject ruleset = value.toObject();
      auto *item = new QListWidgetItem(ruleset.value("name").toString());
      const QString id = ruleset.value("id").toString();
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(m_selectedRulesetIds.contains(id) ? Qt::Checked : Qt::Unchecked);
      item->setData(Qt::UserRole, id);
      item->setData(Qt::UserRole + 1, ruleset);
      m_rulesetList->addItem(item);
    }
    if (m_rulesetList->count() > 0) m_rulesetList->setCurrentRow(0);
    else m_editor->setEnabled(false);
  }, [this](const QString &) {
    // The dialog still works without a project; saved rulesets are just hidden.
    m_editor->setEnabled(false);
  });
}

void ContentBlockingDialog::showRulesetRow(int row) {
  m_filling = true;
  if (row < 0 || row >= m_rulesetList->count()) {
    m_editorItem = nullptr;
    m_editor->setEnabled(false);
    m_filling = false;
    return;
  }
  m_editorItem = m_rulesetList->item(row);
  m_editor->setEnabled(true);
  const QJsonObject ruleset = m_editorItem->data(Qt::UserRole + 1).toJsonObject();
  m_rulesetName->setText(ruleset.value("name").toString());
  m_rulesText->setPlainText(ruleset.value("rulesText").toString());
  m_actions->clearContents();
  m_actions->setRowCount(0);
  for (const auto &actionValue : ruleset.value("actions").toArray()) {
    const QJsonObject action = actionValue.toObject();
    const int newRow = m_actions->rowCount();
    m_actions->insertRow(newRow);
    m_actions->setItem(newRow, 0, new QTableWidgetItem(joinedDomains(action.value("domains"))));
    m_actions->setItem(newRow, 1, new QTableWidgetItem(action.value("selector").toString()));
    auto *actionCombo = new QComboBox();
    actionCombo->addItem("Hide", "hide");
    actionCombo->addItem("Click", "click");
    actionCombo->setCurrentIndex(qMax(0, actionCombo->findData(action.value("action").toString())));
    connect(actionCombo, &QComboBox::currentIndexChanged, this, [this] { markDirty(); });
    m_actions->setCellWidget(newRow, 2, actionCombo);
    m_actions->setItem(newRow, 3,
                       new QTableWidgetItem(QString::number(action.value("delayMs").toInt())));
  }
  m_error->hide();
  m_filling = false;
}

void ContentBlockingDialog::markDirty() {
  if (!m_editorItem || m_filling) return;
  QJsonObject staged = editorRuleset();
  staged.insert("__staged", true);
  m_editorItem->setData(Qt::UserRole + 1, staged);
  const QString name = staged.value("name").toString();
  m_editorItem->setText(name.isEmpty() ? QStringLiteral("Untitled ruleset *") : name + " *");
}

QJsonObject ContentBlockingDialog::editorRuleset() const {
  QJsonArray actions;
  for (int row = 0; row < m_actions->rowCount(); ++row) {
    const auto *actionCombo = qobject_cast<const QComboBox *>(m_actions->cellWidget(row, 2));
    const QTableWidgetItem *siteCell = m_actions->item(row, 0);
    const QTableWidgetItem *selectorCell = m_actions->item(row, 1);
    const QTableWidgetItem *delayCell = m_actions->item(row, 3);
    QJsonArray domains;
    for (const QString &part :
         (siteCell ? siteCell->text() : QString()).split(',', Qt::SkipEmptyParts)) {
      domains.append(part.trimmed().toLower());
    }
    actions.append(QJsonObject{{"domains", domains},
                               {"selector", selectorCell ? selectorCell->text().trimmed() : QString()},
                               {"action", actionCombo ? actionCombo->currentData().toString()
                                                      : QStringLiteral("hide")},
                               {"delayMs", delayCell ? delayCell->text().toInt() : 0}});
  }
  return QJsonObject{{"id", m_editorItem ? m_editorItem->data(Qt::UserRole).toString() : QString()},
                     {"kind", "custom"},
                     {"name", m_rulesetName->text().trimmed()},
                     {"rulesText", m_rulesText->toPlainText()},
                     {"actions", actions}};
}

QString ContentBlockingDialog::validateRuleset(const QJsonObject &ruleset) const {
  static const QRegularExpression domainPattern(QStringLiteral("^[a-z0-9*_.\\-]+$"));
  static const QRegularExpression selectorPattern(
      QStringLiteral("^[A-Za-z0-9_\\-.>#\\[\\]=|^$:*~\\s,\"']+$"));
  const QString name = ruleset.value("name").toString();
  if (name.isEmpty()) return "Every custom ruleset needs a name.";
  const QString rulesText = ruleset.value("rulesText").toString();
  if (rulesText.size() > 10 * 1024 * 1024) return "The filter text exceeds 10 MiB.";
  const QJsonArray actions = ruleset.value("actions").toArray();
  if (actions.size() > 200) return "A ruleset may not define more than 200 actions.";
  if (rulesText.trimmed().isEmpty() && actions.isEmpty()) {
    return "Add at least one filter line or one structured action.";
  }
  for (int index = 0; index < actions.size(); ++index) {
    const QJsonObject action = actions.at(index).toObject();
    const QJsonArray domains = action.value("domains").toArray();
    if (domains.isEmpty() || domains.size() > 8) {
      return QStringLiteral("Action %1 needs between 1 and 8 domain names.").arg(index + 1);
    }
    for (const auto &domain : domains) {
      if (!domainPattern.match(domain.toString()).hasMatch()) {
        return QStringLiteral("Action %1 has an invalid domain: %2")
            .arg(index + 1)
            .arg(domain.toString());
      }
    }
    const QString selector = action.value("selector").toString();
    if (selector.isEmpty() || selector.size() > 200 || selector.contains("..") ||
        !selectorPattern.match(selector).hasMatch()) {
      return QStringLiteral("Action %1 has an unsupported selector.").arg(index + 1);
    }
    const qint64 delayMs = static_cast<qint64>(action.value("delayMs").toDouble(-1));
    if (delayMs < 0 || delayMs > 5000) {
      return QStringLiteral("Action %1 delay must be between 0 and 5000 ms.").arg(index + 1);
    }
  }
  return {};
}

void ContentBlockingDialog::acceptDialog() {
  // Validate every ruleset, staged or not.
  for (int row = 0; row < m_rulesetList->count(); ++row) {
    const QJsonObject ruleset =
        m_rulesetList->item(row)->data(Qt::UserRole + 1).toJsonObject();
    if (ruleset.contains("__staged")) {
      const QString error = validateRuleset(ruleset);
      if (!error.isEmpty()) {
        m_rulesetList->setCurrentRow(row);
        m_error->setText(error);
        m_error->show();
        return;
      }
    }
  }

  // Site exceptions must look like domains before anything is saved.
  static const QRegularExpression domainPattern(QStringLiteral("^[a-z0-9*_.\\-]+$"));
  const QStringList exceptionParts = m_disabledDomains->text().split(',', Qt::SkipEmptyParts);
  for (const QString &part : exceptionParts) {
    const QString domain = part.trimmed().toLower();
    if (domain.isEmpty() || !domainPattern.match(domain).hasMatch()) {
      m_error->setText(QStringLiteral("'%1' is not a valid site exception domain.")
                           .arg(part.trimmed()));
      m_error->show();
      return;
    }
  }

  QList<QJsonObject> pending;
  for (int row = 0; row < m_rulesetList->count(); ++row) {
    QJsonObject staged = m_rulesetList->item(row)->data(Qt::UserRole + 1).toJsonObject();
    if (!staged.contains("__staged")) continue;
    staged.remove("__staged");
    pending.append(staged);
  }
  if (pending.isEmpty() || !m_rpc) return accept();

  auto remaining = std::make_shared<int>(pending.size());
  auto failed = std::make_shared<bool>(false);
  for (const QJsonObject &ruleset : pending) {
    m_rpc("contentRuleset.save", {{"ruleset", ruleset}},
          [this, remaining, failed](const QJsonObject &) {
            if (*failed) return;
            *remaining -= 1;
            if (*remaining == 0) accept();
          },
          [remaining, failed](const QString &message) {
            *failed = true;
            QMessageBox::warning(nullptr, "Could not save ruleset", message);
          });
  }
}

QJsonObject ContentBlockingDialog::settings() const {
  QJsonArray subscriptionIds;
  for (int index = 0; index < m_subscriptionChecks.size(); ++index) {
    if (m_subscriptionChecks.at(index)->isChecked()) {
      subscriptionIds.append(m_subscriptionIds.at(index));
    }
  }
  QJsonArray disabledDomains;
  for (const QString &part : m_disabledDomains->text().split(',', Qt::SkipEmptyParts)) {
    disabledDomains.append(part.trimmed().toLower());
  }
  QJsonArray customRulesetIds;
  for (int row = 0; row < m_rulesetList->count(); ++row) {
    const QListWidgetItem *item = m_rulesetList->item(row);
    if (item->checkState() == Qt::Checked && !item->data(Qt::UserRole).toString().isEmpty()) {
      customRulesetIds.append(item->data(Qt::UserRole).toString());
    }
  }
  return QJsonObject{{"enabled", m_enabled},
                     {"consentStrategy", m_consentStrategy->currentData().toString()},
                     {"subscriptionIds", subscriptionIds},
                     {"customRulesetIds", customRulesetIds},
                     {"versionPolicy", "latest"},
                     {"disabledDomains", disabledDomains}};
}

} // namespace CyberSnapper
