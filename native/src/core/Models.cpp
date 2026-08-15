#include "core/Models.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace CyberSnapper {

namespace {

QStringList stringList(const QJsonValue &value) {
  QStringList out;
  for (const auto &entry : value.toArray()) {
    if (entry.isString() && !entry.toString().trimmed().isEmpty()) {
      out.append(entry.toString().trimmed().left(4096));
      if (out.size() >= 1000) break;
    }
  }
  return out;
}

QJsonArray jsonList(const QStringList &values) {
  QJsonArray out;
  for (const auto &value : values) out.append(value);
  return out;
}

int boundedInt(const QJsonObject &o, const char *key, int fallback, int low, int high) {
  const int value = o.value(QLatin1String(key)).toInt(fallback);
  return qBound(low, value, high);
}

double boundedDouble(const QJsonObject &o, const char *key, double fallback, double low, double high) {
  const double value = o.value(QLatin1String(key)).toDouble(fallback);
  return qBound(low, value, high);
}

PresentationSettings presentationFromJson(const QJsonValue &value) {
  PresentationSettings settings;
  if (!value.isObject()) return settings;
  const QJsonObject object = value.toObject();
  settings.enabled = object.value("enabled").toBool(false);
  settings.scene = object.value("scene").toString(settings.scene);
  if (!QStringList{"clean", "aurora", "sunset", "midnight", "graphite", "customSolid"}.contains(settings.scene)) {
    settings.scene = "aurora";
  }
  settings.frame = object.value("frame").toString(settings.frame);
  if (!QStringList{"auto", "none", "roundedCard", "lightBrowser", "darkBrowser", "lightTablet", "darkTablet", "lightPhone", "darkPhone"}.contains(settings.frame)) {
    settings.frame = "auto";
  }
  settings.aspect = object.value("aspect").toString(settings.aspect);
  if (!QStringList{"auto", "16:9", "4:3", "square"}.contains(settings.aspect)) settings.aspect = "auto";
  settings.padding = object.value("padding").toString(settings.padding);
  if (!QStringList{"compact", "balanced", "generous"}.contains(settings.padding)) settings.padding = "balanced";
  settings.shadow = object.value("shadow").toString(settings.shadow);
  if (!QStringList{"none", "soft", "strong"}.contains(settings.shadow)) settings.shadow = "soft";
  const QString color = object.value("solidColor").toString(settings.solidColor).trimmed().toUpper();
  settings.solidColor = QRegularExpression("^#[0-9A-F]{6}$").match(color).hasMatch() ? color : "#0B1220";
  return settings;
}

QJsonObject presentationToJson(const PresentationSettings &settings) {
  return {{"enabled", settings.enabled}, {"scene", settings.scene}, {"frame", settings.frame},
          {"aspect", settings.aspect}, {"padding", settings.padding}, {"shadow", settings.shadow},
          {"solidColor", settings.solidColor}};
}

} // namespace

CaptureProfile defaultProfile() {
  CaptureProfile profile;
  profile.id = "default";
  profile.name = "Default";
  profile.viewports = {
      {"desktop", "Desktop", 1920, 1080, 1.0, false, true},
      {"tablet", "Tablet", 768, 1024, 1.0, true, true},
      {"mobile", "Mobile", 375, 812, 1.0, true, true},
  };
  return profile;
}

QJsonObject toJson(const Viewport &viewport) {
  return {{"id", viewport.id},
          {"name", viewport.name},
          {"width", viewport.width},
          {"height", viewport.height},
          {"deviceScaleFactor", viewport.deviceScaleFactor},
          {"mobile", viewport.mobile},
          {"enabled", viewport.enabled}};
}

Viewport viewportFromJson(const QJsonObject &object) {
  Viewport viewport;
  viewport.id = object.value("id").toString(newId());
  viewport.name = object.value("name").toString("Viewport").trimmed().left(64);
  viewport.width = boundedInt(object, "width", 1920, 64, 16384);
  viewport.height = boundedInt(object, "height", 1080, 64, 16384);
  viewport.deviceScaleFactor = boundedDouble(object, "deviceScaleFactor", 1.0, 0.5, 4.0);
  viewport.mobile = object.value("mobile").toBool(false);
  viewport.enabled = object.value("enabled").toBool(true);
  return viewport;
}

QJsonObject toJson(const CaptureProfile &profile) {
  QJsonArray viewports;
  for (const auto &viewport : profile.viewports) viewports.append(toJson(viewport));
  return {{"id", profile.id},
          {"name", profile.name},
          {"viewports", viewports},
          {"engines", jsonList(profile.engines)},
          {"formats", jsonList(profile.formats)},
          {"captureMode", profile.captureMode},
          {"elementSelector", profile.elementSelector},
          {"initialDelay", profile.initialDelay},
          {"scrollDelay", profile.scrollDelay},
          {"finalDelay", profile.finalDelay},
          {"concurrency", profile.concurrency},
          {"navigationTimeoutSeconds", profile.navigationTimeoutSeconds},
          {"selectorTimeoutSeconds", profile.selectorTimeoutSeconds},
          {"maxScrollSeconds", profile.maxScrollSeconds},
          {"maxPageHeight", profile.maxPageHeight},
          {"blockPopups", profile.blockPopups},
          {"stripWhitespace", profile.stripWhitespace},
          {"blocklist", jsonList(profile.blocklist)},
          {"hideSelectors", jsonList(profile.hideSelectors)},
          {"waitForSelector", profile.waitForSelector},
          {"namingTemplate", profile.namingTemplate},
          {"collisionPolicy", profile.collisionPolicy},
          {"webpQuality", profile.webpQuality},
          {"avifQuality", profile.avifQuality},
          {"pdfFormat", profile.pdfFormat},
          {"pdfLandscape", profile.pdfLandscape},
          {"pdfMargin", profile.pdfMargin},
          {"comparisonEnabled", profile.comparisonEnabled},
          {"pixelThreshold", profile.pixelThreshold},
          {"mismatchThreshold", profile.mismatchThreshold},
          {"comparisonIgnoreSelectors", jsonList(profile.comparisonIgnoreSelectors)},
          {"presentation", presentationToJson(profile.presentation)}};
}

CaptureProfile profileFromJson(const QJsonObject &object) {
  CaptureProfile profile = defaultProfile();
  profile.id = object.value("id").toString(profile.id).trimmed().left(128);
  profile.name = object.value("name").toString(profile.name).trimmed().left(128);
  const auto viewportValues = object.value("viewports").toArray();
  if (!viewportValues.isEmpty()) {
    profile.viewports.clear();
    for (const auto &value : viewportValues) {
      if (value.isObject()) profile.viewports.append(viewportFromJson(value.toObject()));
      if (profile.viewports.size() >= 100) break;
    }
  }
  const auto engines = stringList(object.value("engines"));
  if (!engines.isEmpty()) { profile.engines = engines; profile.engines.removeDuplicates(); }
  const auto formats = stringList(object.value("formats"));
  if (!formats.isEmpty()) { profile.formats = formats; profile.formats.removeDuplicates(); }
  profile.captureMode = object.value("captureMode").toString(profile.captureMode);
  if (!QStringList{"fullPage", "viewport", "element"}.contains(profile.captureMode)) profile.captureMode = "fullPage";
  profile.elementSelector = object.value("elementSelector").toString().trimmed().left(4096);
  profile.initialDelay = boundedDouble(object, "initialDelay", profile.initialDelay, 0.0, 300.0);
  profile.scrollDelay = boundedDouble(object, "scrollDelay", profile.scrollDelay, 0.0, 300.0);
  profile.finalDelay = boundedDouble(object, "finalDelay", profile.finalDelay, 0.0, 300.0);
  profile.concurrency = boundedInt(object, "concurrency", profile.concurrency, 1, 10);
  profile.navigationTimeoutSeconds = boundedInt(object, "navigationTimeoutSeconds", 60, 1, 600);
  profile.selectorTimeoutSeconds = boundedInt(object, "selectorTimeoutSeconds", 30, 1, 300);
  profile.maxScrollSeconds = boundedInt(object, "maxScrollSeconds", 120, 5, 1800);
  profile.maxPageHeight = boundedInt(object, "maxPageHeight", 100000, 1000, 1000000);
  profile.blockPopups = object.value("blockPopups").toBool(profile.blockPopups);
  profile.stripWhitespace = object.value("stripWhitespace").toBool(profile.stripWhitespace);
  profile.blocklist = stringList(object.value("blocklist"));
  profile.hideSelectors = stringList(object.value("hideSelectors"));
  profile.waitForSelector = object.value("waitForSelector").toString().trimmed().left(4096);
  profile.namingTemplate = object.value("namingTemplate").toString(profile.namingTemplate).trimmed().left(512);
  if (profile.namingTemplate.isEmpty()) profile.namingTemplate = "{hostname}-{preset}";
  profile.collisionPolicy = object.value("collisionPolicy").toString(profile.collisionPolicy);
  if (!QStringList{"version", "overwrite", "skip"}.contains(profile.collisionPolicy)) profile.collisionPolicy = "version";
  profile.webpQuality = boundedInt(object, "webpQuality", 80, 1, 100);
  profile.avifQuality = boundedInt(object, "avifQuality", 50, 1, 100);
  profile.pdfFormat = object.value("pdfFormat").toString("A4");
  profile.pdfLandscape = object.value("pdfLandscape").toBool(false);
  profile.pdfMargin = object.value("pdfMargin").toString("0");
  profile.comparisonEnabled = object.value("comparisonEnabled").toBool(false);
  profile.pixelThreshold = boundedDouble(object, "pixelThreshold", 0.10, 0.0, 1.0);
  profile.mismatchThreshold = boundedDouble(object, "mismatchThreshold", 0.001, 0.0, 1.0);
  profile.comparisonIgnoreSelectors = stringList(object.value("comparisonIgnoreSelectors"));
  profile.presentation = presentationFromJson(object.value("presentation"));
  return profile;
}

QJsonObject toJson(const CaptureTarget &target) {
  return {{"id", target.id},
          {"name", target.name},
          {"url", target.url},
          {"targetSetId", target.targetSetId},
          {"targetSetName", target.targetSetName},
          {"enabled", target.enabled}};
}

CaptureTarget captureTargetFromJson(const QJsonObject &object) {
  CaptureTarget target;
  target.id = object.value("id").toString(newId()).trimmed().left(128);
  target.name = object.value("name").toString().trimmed().left(256);
  target.url = object.value("url").toString().trimmed().left(4096);
  target.targetSetId = object.value("targetSetId").toString().trimmed().left(128);
  target.targetSetName = object.value("targetSetName").toString().trimmed().left(256);
  target.enabled = object.value("enabled").toBool(true);
  return target;
}

QJsonObject toJson(const JobRequest &request) {
  QJsonArray targets;
  for (const auto &target : request.targets) targets.append(toJson(target));
  return {{"id", request.id},
          {"projectId", request.projectId},
          {"projectRoot", request.projectRoot},
          {"profileId", request.profileId},
          {"source", request.source},
          {"urls", jsonList(request.urls)},
          {"targetSetId", request.targetSetId},
          {"targets", targets},
          {"profile", toJson(request.profile)},
          {"baselines", request.baselines},
          {"allowLocalhost", request.allowLocalhost}};
}

JobRequest jobRequestFromJson(const QJsonObject &object) {
  JobRequest request;
  request.id = object.value("id").toString(newId());
  request.projectId = object.value("projectId").toString();
  request.projectRoot = object.value("projectRoot").toString();
  request.profileId = object.value("profileId").toString("default");
  request.source = object.value("source").toString("gui");
  request.urls = stringList(object.value("urls"));
  request.targetSetId = object.value("targetSetId").toString().trimmed().left(128);
  for (const auto &value : object.value("targets").toArray()) {
    if (value.isObject()) request.targets.append(captureTargetFromJson(value.toObject()));
    if (request.targets.size() >= 1000) break;
  }
  request.profile = profileFromJson(object.value("profile").toObject());
  request.baselines = object.value("baselines").toObject();
  request.allowLocalhost = object.value("allowLocalhost").toBool(false);
  return request;
}

QJsonObject toJson(const JobRecord &record) {
  return {{"id", record.id},
          {"projectId", record.projectId},
          {"status", record.status},
          {"source", record.source},
          {"createdAt", record.createdAt},
          {"startedAt", record.startedAt},
          {"finishedAt", record.finishedAt},
          {"error", record.error},
          {"completedArtifacts", record.completedArtifacts},
          {"failedArtifacts", record.failedArtifacts},
          {"request", record.request}};
}

QString utcNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString normalizedJobStatus(const QString &status) {
  static const QStringList allowed{"queued", "preparing", "running", "cancelling", "succeeded",
                                   "partial", "failed", "cancelled", "interrupted"};
  return allowed.contains(status) ? status : QStringLiteral("failed");
}

} // namespace CyberSnapper
