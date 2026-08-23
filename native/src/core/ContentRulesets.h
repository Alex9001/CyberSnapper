#pragma once

#include "core/Models.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace CyberSnapper {

class ProjectStore;

struct SubscriptionInfo {
  QString id;
  QString name;
  QString sourceUrl;
  QString license;
  bool defaultEnabled = false;
};

// The curated catalog of community filter lists. Metadata mirrors the official
// uBlock list catalog; lists themselves are downloaded to the app-local cache
// and never fetched during a capture.
const QList<SubscriptionInfo> &subscriptionCatalog();

// Provenance of a content ruleset snapshot written into a project.
struct RulesetReferenceInfo {
  bool enabled = false;
  QString digest;
  QString relativePath;
  QJsonObject sources;
  QStringList warnings;
};

namespace ContentRulesets {

// App-local directory where downloaded community lists are cached.
QString cacheDirectory();
QString cachedListPath(const QString &subscriptionId);

// Combines the profile's enabled subscriptions (from the app-local cache) and
// custom rulesets (from the project database) into a content-addressed
// snapshot at `<project>/.cybersnapper/rulesets/<digest>.json`. Missing or
// invalid sources produce warnings, never hard failures: captures fall back
// to built-in consent handling.
RulesetReferenceInfo buildSnapshot(ProjectStore *store, const CaptureProfile &profile,
                                   QString *error = nullptr);

} // namespace ContentRulesets

} // namespace CyberSnapper
