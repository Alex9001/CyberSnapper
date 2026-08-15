#include "core/Models.h"
#include "core/ProjectStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QTextStream>
#include <QThread>

#include <cmath>

using namespace CyberSnapper;

namespace {

constexpr auto ShowcaseUrl = "https://cyberbrand.net/";

QString sourceCapture(const QString &name) {
  return QStringLiteral(CYBERSNAPPER_SOURCE_ROOT "/docs/fixtures/") + name;
}

bool copySourceCapture(const QString &name, const QString &path) {
  const QImage image(sourceCapture(name));
  if (image.isNull()) return false;
  QDir().mkpath(QFileInfo(path).absolutePath());
  return image.save(path, "PNG");
}

struct DiffResult {
  bool ok = false;
  qint64 mismatchedPixels = 0;
  qint64 analyzedPixels = 0;
  int width = 0;
  int height = 0;
};

DiffResult drawDiff(const QString &baselinePath, const QString &currentPath, const QString &path,
                    double pixelThreshold) {
  const QImage baselineSource(baselinePath);
  const QImage currentSource(currentPath);
  if (baselineSource.isNull() || currentSource.isNull() || baselineSource.size() != currentSource.size()) return {};
  const QImage baseline = baselineSource.convertToFormat(QImage::Format_RGBA8888);
  const QImage current = currentSource.convertToFormat(QImage::Format_RGBA8888);
  QImage image(baseline.size(), QImage::Format_RGBA8888);
  DiffResult result{true, 0, qint64(image.width()) * image.height(), image.width(), image.height()};
  const double channelThreshold = pixelThreshold * 255.0;
  for (int y = 0; y < image.height(); ++y) {
    const uchar *left = baseline.constScanLine(y);
    const uchar *right = current.constScanLine(y);
    uchar *output = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      const int offset = x * 4;
      const int delta = qMax(qMax(std::abs(int(left[offset]) - int(right[offset])),
                                  std::abs(int(left[offset + 1]) - int(right[offset + 1]))),
                             qMax(std::abs(int(left[offset + 2]) - int(right[offset + 2])),
                                  std::abs(int(left[offset + 3]) - int(right[offset + 3]))));
      if (delta > channelThreshold) {
        ++result.mismatchedPixels;
        output[offset] = 255;
        output[offset + 1] = 0;
        output[offset + 2] = 96;
      } else {
        const int gray = (int(right[offset]) + int(right[offset + 1]) + int(right[offset + 2])) / 6 + 96;
        output[offset] = uchar(gray);
        output[offset + 1] = uchar(gray);
        output[offset + 2] = uchar(gray);
      }
      output[offset + 3] = 255;
    }
  }
  QDir().mkpath(QFileInfo(path).absolutePath());
  result.ok = image.save(path, "PNG");
  return result;
}

QJsonObject artifact(const QString &id, const QString &relativePath, const QString &viewport,
                     const QString &engine = "chromium", const QString &status = "succeeded") {
  const int width = viewport == "Mobile" ? 390 : viewport == "Tablet" ? 768 : 1440;
  const int height = viewport == "Mobile" ? 844 : viewport == "Tablet" ? 1024 : 900;
  return {{"id", id}, {"url", ShowcaseUrl}, {"engine", engine}, {"viewportId", viewport.toLower()},
          {"targetId", "home"}, {"targetName", "Homepage"}, {"targetSetId", "target-set-production"},
          {"targetSetName", "Production site"},
          {"viewportName", viewport}, {"captureMode", "fullPage"}, {"format", "png"},
          {"relativePath", relativePath}, {"width", width}, {"height", height}, {"sha256", id + "-fixture"},
          {"status", status}, {"error", status == "failed" ? "Navigation timed out" : QString{}},
          {"createdAt", utcNow()}};
}

bool addJob(ProjectStore &store, const CaptureProfile &profile, const QString &id, const QString &source,
            const QString &status, int completed, int failed, const QList<QJsonObject> &artifacts,
            const QString &message = {}) {
  JobRequest request;
  request.id = id;
  request.projectId = store.projectId();
  request.projectRoot = store.root();
  request.profileId = profile.id;
  request.source = source;
  request.urls = {ShowcaseUrl, "https://cyberbrand.net/pricing"};
  request.profile = profile;
  QString error;
  if (!store.insertJob(request, &error) || !store.updateJob(id, "running")) return false;
  for (const auto &value : artifacts) if (!store.insertArtifact(id, value)) return false;
  if (!store.updateJob(id, status, message, completed, failed)) return false;
  QThread::msleep(3);
  return true;
}

int fail(const QString &message) {
  QTextStream(stderr) << "cybersnapper-doc-fixture: " << message << '\n';
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  application.setOrganizationName("CyberBrand");
  application.setApplicationName("CyberSnapper Documentation Fixture");
  if (application.arguments().size() != 2) return fail("provide the fixture project directory");
  const QString root = QFileInfo(application.arguments().at(1)).absoluteFilePath();

  ProjectStore store;
  QString error;
  if (!store.create(root, "CYBER BRAND Portfolio", &error)) return fail(error);
  CaptureProfile profile = defaultProfile();
  profile.name = "Portfolio Capture";
  profile.viewports = {{"desktop", "Desktop", 1440, 900, 1.0, false, true},
                       {"tablet", "Tablet", 768, 1024, 1.0, true, true},
                       {"mobile", "Mobile", 390, 844, 2.0, true, true}};
  profile.engines = {"chromium", "firefox"};
  profile.formats = {"png", "webp"};
  profile.concurrency = 2;
  profile.blockPopups = true;
  profile.presentation.enabled = true;
  profile.presentation.scene = "aurora";
  profile.presentation.frame = "darkTablet";
  profile.presentation.aspect = "16:9";
  profile.presentation.padding = "balanced";
  profile.presentation.shadow = "soft";
  profile.comparisonEnabled = true;
  profile.comparisonIgnoreSelectors = {".timestamp", ".rotating-promo"};
  if (!store.saveProfile(profile, &error)) return fail(error);
  if (store.saveTargetSet({{"id", "target-set-production"}, {"name", "Portfolio projects"},
      {"description", "Websites and pages ready for portfolio capture"},
      {"targets", QJsonArray{QJsonObject{{"id", "home"}, {"label", "Homepage"}, {"url", ShowcaseUrl}, {"enabled", true}},
                               QJsonObject{{"id", "pricing"}, {"label", "Pricing"}, {"url", "https://cyberbrand.net/pricing"}, {"enabled", true}},
                               QJsonObject{{"id", "journal"}, {"label", "Insights"}, {"url", "https://cyberbrand.net/blog"}, {"enabled", false}}}}}, &error).isEmpty()) return fail(error);
  if (store.saveTargetSet({{"id", "target-set-campaign"}, {"name", "Case study pages"},
      {"description", "Alternate pages used in detailed project stories"},
      {"targets", QJsonArray{QJsonObject{{"id", "detail"}, {"label", "The Forge"}, {"url", "https://cyberbrand.net/forge/"}, {"enabled", true}}}}}, &error).isEmpty()) return fail(error);

  const QString baseRelative = "captures/showcase/baseline.png";
  const QString currentRelative = "captures/showcase/current.png";
  const QString tabletRelative = "captures/showcase/tablet-dark.png";
  const QString mobileRelative = "captures/showcase/mobile-dark.png";
  const QString diffRelative = "captures/showcase/difference.png";
  const QString baselinePath = QDir(root).filePath(baseRelative);
  const QString currentPath = QDir(root).filePath(currentRelative);
  if (!copySourceCapture("cyberbrand-light-desktop.png", baselinePath) ||
      !copySourceCapture("cyberbrand-dark-desktop.png", currentPath) ||
      !copySourceCapture("cyberbrand-dark-tablet.png", QDir(root).filePath(tabletRelative)) ||
      !copySourceCapture("cyberbrand-dark-mobile.png", QDir(root).filePath(mobileRelative))) {
    return fail("could not copy CYBER BRAND source captures");
  }
  const DiffResult diff = drawDiff(baselinePath, currentPath, QDir(root).filePath(diffRelative), profile.pixelThreshold);
  if (!diff.ok) return fail("could not compare CYBER BRAND source captures");

  if (!addJob(store, profile, "job-api-failure", "api", "failed", 0, 1, {}, "DNS lookup failed")) return fail("could not add API job");
  if (!addJob(store, profile, "job-weekday-schedule", "schedule:weekday", "succeeded", 12, 0,
              {artifact("artifact-schedule", tabletRelative, "Tablet", "firefox")})) return fail("could not add scheduled job");
  if (!addJob(store, profile, "job-partial", "gui", "partial", 5, 1,
              {artifact("artifact-partial", mobileRelative, "Mobile"),
               artifact("artifact-timeout", "captures/showcase/missing.png", "Desktop", "firefox", "failed")},
              "One Firefox target timed out")) return fail("could not add partial job");
  if (!addJob(store, profile, "job-baseline", "gui", "succeeded", 1, 0,
              {artifact("artifact-baseline", baseRelative, "Desktop")})) return fail("could not add baseline job");
  if (!addJob(store, profile, "job-visual-change", "schedule:release-watch", "succeeded", 1, 0,
              {artifact("artifact-current", currentRelative, "Desktop")})) return fail("could not add comparison job");

  const QString key = QString(ShowcaseUrl) + "|chromium|desktop|fullPage|png";
  if (!store.setBaseline(key, "artifact-baseline", baseRelative)) return fail("could not set baseline");
  if (!store.insertComparison({{"id", "comparison-release"}, {"jobId", "job-visual-change"},
                               {"comparisonKey", key}, {"baselineArtifactId", "artifact-baseline"},
                               {"currentArtifactId", "artifact-current"}, {"status", "changed"},
                               {"mismatchRatio", double(diff.mismatchedPixels) / double(diff.analyzedPixels)}, {"diffRelativePath", diffRelative},
                               {"url", ShowcaseUrl}, {"targetId", "home"}, {"targetName", "Homepage"},
                               {"targetSetId", "target-set-production"}, {"targetSetName", "Production site"},
                               {"engine", "chromium"}, {"viewportId", "desktop"}, {"viewportName", "Desktop"},
                               {"captureMode", "fullPage"}, {"format", "png"},
                               {"analysisWidth", diff.width}, {"analysisHeight", diff.height}, {"analysisScale", 1.0},
                               {"mismatchedPixels", diff.mismatchedPixels}, {"analyzedPixels", diff.analyzedPixels}, {"algorithmVersion", 2},
                               {"baselineRelativePath", baseRelative},
                               {"createdAt", utcNow()}})) return fail("could not add comparison");
  if (!store.insertComparison({{"id", "comparison-mobile-baseline"}, {"jobId", "job-partial"},
                               {"comparisonKey", QString(ShowcaseUrl) + "|chromium|mobile|fullPage|png"},
                               {"currentArtifactId", "artifact-partial"}, {"status", "missing_baseline"},
                               {"url", ShowcaseUrl}, {"targetId", "home"}, {"targetName", "Homepage"},
                               {"targetSetId", "target-set-production"}, {"targetSetName", "Production site"},
                               {"engine", "chromium"}, {"viewportId", "mobile"}, {"viewportName", "Mobile"},
                               {"captureMode", "fullPage"}, {"format", "png"}, {"analysisWidth", 390},
                               {"analysisHeight", 844}, {"analysisScale", 1.0}, {"createdAt", utcNow()}})) return fail("could not add missing baseline");

  const QJsonArray urls{ShowcaseUrl, "https://cyberbrand.net/pricing"};
  const QList<QJsonObject> schedules{
      {{"id", "schedule-release"}, {"name", "Release candidate watch"}, {"enabled", true},
       {"profileId", "default"}, {"targetSetId", "target-set-production"}, {"urls", QJsonArray{}},
       {"recurrence", QJsonObject{{"type", "weekly"}, {"weekdays", QJsonArray{1, 2, 3, 4, 5}},
                                   {"time", "08:30"}, {"timeZone", "America/Los_Angeles"}}},
       {"lastRun", "2026-08-14T15:30:00.000Z"}, {"nextRun", "2030-01-02T16:30:00.000Z"}, {"lastStatus", "succeeded"}},
      {{"id", "schedule-nightly"}, {"name", "Nightly visual archive"}, {"enabled", true},
       {"profileId", "default"}, {"urls", urls},
       {"recurrence", QJsonObject{{"type", "daily"}, {"time", "02:00"}, {"timeZone", "UTC"}}},
       {"lastRun", "2026-08-14T02:00:00.000Z"}, {"nextRun", "2030-01-03T02:00:00.000Z"}, {"lastStatus", "succeeded"}},
      {{"id", "schedule-monthly"}, {"name", "Monthly compliance archive"}, {"enabled", false},
       {"profileId", "default"}, {"urls", QJsonArray{ShowcaseUrl}},
       {"recurrence", QJsonObject{{"type", "monthly"}, {"day", 1}, {"time", "09:00"}, {"timeZone", "UTC"}}},
       {"lastRun", "2026-08-01T09:00:00.000Z"}, {"nextRun", "2030-02-01T09:00:00.000Z"}, {"lastStatus", "succeeded"}},
  };
  for (const auto &schedule : schedules) if (!store.upsertSchedule(schedule, &error)) return fail(error);
  QTextStream(stdout) << root << '\n';
  return 0;
}
