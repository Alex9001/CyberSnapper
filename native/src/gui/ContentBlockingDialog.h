#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QStringList>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QListWidget;
class QTableWidget;
class QTextEdit;

namespace CyberSnapper {

// Configuration dialog for profile content blocking: curated subscription
// lists, consent handling strategy, site exceptions, and reusable custom
// rulesets. Edits are validated inline; custom rulesets persist to the open
// project through the agent RPC when the dialog is accepted.
class ContentBlockingDialog final : public QDialog {
  Q_OBJECT
public:
  using RpcInvoker =
      std::function<void(const QString &method, const QJsonObject &params,
                         std::function<void(const QJsonObject &)> success,
                         std::function<void(const QString &)> failure)>;
  ContentBlockingDialog(const QJsonObject &settings, const RpcInvoker &rpc, QWidget *parent = nullptr);

  QJsonObject settings() const;

private:
  void loadRulesets();
  void refreshSubscriptionStatus();
  void showRulesetRow(int row);
  void markDirty();
  QString validateRuleset(const QJsonObject &ruleset) const;
  QJsonObject editorRuleset() const;
  void acceptDialog();

  RpcInvoker m_rpc;
  bool m_enabled = true;
  QList<QCheckBox *> m_subscriptionChecks;
  QStringList m_subscriptionIds;
  QStringList m_selectedRulesetIds;
  QComboBox *m_consentStrategy = nullptr;
  QLineEdit *m_disabledDomains = nullptr;
  QListWidget *m_rulesetList = nullptr;
  QWidget *m_editor = nullptr;
  QListWidgetItem *m_editorItem = nullptr;
  bool m_filling = false;
  QLineEdit *m_rulesetName = nullptr;
  QTextEdit *m_rulesText = nullptr;
  QTableWidget *m_actions = nullptr;
  QLabel *m_error = nullptr;
};

} // namespace CyberSnapper
