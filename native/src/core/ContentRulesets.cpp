#include "core/ContentRulesets.h"

#include "core/Paths.h"
#include "core/ProjectStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace CyberSnapper {

namespace {

const QList<SubscriptionInfo> &catalog() {
  static const QList<SubscriptionInfo> subscriptions{
      {QStringLiteral("easylist-cookie"), QStringLiteral("EasyList Cookie"),
       QStringLiteral("https://easylist-downloads.adblockplus.org/easylist_cookie.txt"),
       QStringLiteral("CC BY-SA 3.0"), true},
      {QStringLiteral("ublock-cookie"), QStringLiteral("uBlock Cookie Notices"),
       QStringLiteral(
           "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/cookies.txt"),
       QStringLiteral("GPLv3"), true},
      {QStringLiteral("easylist-ads"), QStringLiteral("EasyList"),
       QStringLiteral("https://easylist.to/easylist/easylist.txt"),
       QStringLiteral("CC BY-SA 3.0"), false},
      {QStringLiteral("easyprivacy"), QStringLiteral("EasyPrivacy"),
       QStringLiteral("https://easylist.to/easylist/easyprivacy.txt"),
       QStringLiteral("CC BY-SA 3.0"), false},
      {QStringLiteral("ublock-annoyances"), QStringLiteral("uBlock Annoyances"),
       QStringLiteral(
           "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/annoyances.txt"),
       QStringLiteral("GPLv3"), false},
  };
  return subscriptions;
}

QString sha256Hex(const QByteArray &bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QStringList splitLines(const QString &text) {
  QStringList lines;
  for (const QString &line : text.split(QLatin1Char('\n'))) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty()) lines.append(trimmed);
  }
  return lines;
}

struct CollectedRules {
  QStringList lines;
  QJsonArray actions;
  QJsonObject sources;
  QStringList warnings;
};

CollectedRules collectSubscriptionRules(const QStringList &subscriptionIds) {
  CollectedRules collected;
  for (const QString &id : subscriptionIds) {
    const SubscriptionInfo *info = nullptr;
    for (const auto &candidate : catalog()) {
      if (candidate.id == id) info = &candidate;
    }
    if (!info) {
      collected.warnings.append(QStringLiteral("Unknown subscription '%1' was skipped").arg(id));
      continue;
    }
    QFile list(ContentRulesets::cachedListPath(id));
    if (!list.open(QIODevice::ReadOnly | QIODevice::Text)) {
      collected.warnings.append(
          QStringLiteral("Subscription '%1' has not been downloaded yet; its rules are not "
                         "included in this capture")
              .arg(info->name));
      continue;
    }
    const QByteArray bytes = list.readAll();
    if (bytes.size() > 10 * 1024 * 1024) {
      collected.warnings.append(
          QStringLiteral("Cached list for '%1' exceeded the 10 MiB limit and was skipped")
              .arg(info->name));
      continue;
    }
    collected.lines.append(splitLines(QString::fromUtf8(bytes)));
    collected.sources.insert(id, QJsonObject{{"name", info->name},
                                             {"digest", sha256Hex(bytes)},
                                             {"license", info->license},
                                             {"sourceUrl", info->sourceUrl}});
  }
  return collected;
}

} // namespace

const QList<SubscriptionInfo> &subscriptionCatalog() { return catalog(); }

namespace ContentRulesets {

QString cacheDirectory() { return QDir(Paths::cacheDir()).filePath(QStringLiteral("rulesets")); }

QString cachedListPath(const QString &subscriptionId) {
  // Subscription ids are fixed catalog identifiers; reject anything unexpected
  // rather than trusting a path component.
  static const QRegularExpression validId{QStringLiteral("^[a-z0-9\\-]+$")};
  if (!validId.match(subscriptionId).hasMatch()) return {};
  return QDir(cacheDirectory()).filePath(subscriptionId + QStringLiteral(".txt"));
}

RulesetReferenceInfo buildSnapshot(ProjectStore *store, const CaptureProfile &profile,
                                   QString *error) {
  RulesetReferenceInfo reference;
  if (!profile.contentBlocking.enabled) return reference;

  CollectedRules collected = collectSubscriptionRules(profile.contentBlocking.subscriptionIds);

  // Custom rulesets live in the project database and travel with the project.
  for (const QString &id : profile.contentBlocking.customRulesetIds) {
    const QJsonObject ruleset = store ? store->contentRuleset(id) : QJsonObject{};
    if (ruleset.isEmpty()) {
      collected.warnings.append(
          QStringLiteral("Custom ruleset '%1' was not found and was skipped").arg(id));
      continue;
    }
    const QString name = ruleset.value("name").toString();
    const QString rulesText = ruleset.value("rulesText").toString();
    collected.lines.append(splitLines(rulesText));
    const QJsonArray actions = ruleset.value("actions").toArray();
    for (const auto &action : actions) {
      if (action.isObject()) collected.actions.append(action.toObject());
    }
    if (collected.actions.size() < actions.size()) {
      collected.warnings.append(
          QStringLiteral("Custom ruleset '%1' contained malformed actions that were skipped")
              .arg(name));
    }
    collected.sources.insert(id, QJsonObject{{"name", name},
                                             {"kind", ruleset.value("kind").toString()},
                                             {"digest", sha256Hex(rulesText.toUtf8())}});
  }

  qsizetype totalBytes = 0;
  for (const QString &line : collected.lines) totalBytes += line.size() + 1;
  if (totalBytes > 32 * 1024 * 1024) {
    collected.warnings.append(
        QStringLiteral("Combined filter text exceeded the 32 MiB limit; content blocking is "
                       "limited to built-in consent handling for this capture"));
    collected.lines.clear();
    collected.actions = {};
    collected.sources = {};
  }

  QJsonObject payloadObject;
  payloadObject.insert("generatedAt", utcNow());
  payloadObject.insert("subscriptions", collected.sources);
  QJsonArray rulesJson;
  for (const QString &line : collected.lines) rulesJson.append(line);
  payloadObject.insert("rulesText", rulesJson);
  payloadObject.insert("actions", collected.actions);
  const QString payload =
      QString::fromUtf8(QJsonDocument(payloadObject).toJson(QJsonDocument::Compact));

  const QString digest = sha256Hex(payload.toUtf8());
  const QString relativePath = QStringLiteral(".cybersnapper/rulesets/%1.json").arg(digest);
  const QString projectRoot = store ? store->root() : QString{};
  if (projectRoot.isEmpty()) {
    if (error) *error = "A writable project is required";
    return reference;
  }
  QSaveFile file(QDir(projectRoot).filePath(relativePath));
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = file.errorString();
    return reference;
  }
  const QJsonObject envelope{{"format", 1}, {"digest", digest}, {"payload", payload}};
  file.write(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
  if (!file.commit()) {
    if (error) *error = file.errorString();
    return reference;
  }

  reference.enabled = true;
  reference.digest = digest;
  reference.relativePath = relativePath;
  reference.sources = collected.sources;
  reference.warnings = collected.warnings;
  return reference;
}

} // namespace ContentRulesets

} // namespace CyberSnapper
