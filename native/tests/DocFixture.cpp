#include "core/Models.h"
#include "core/ProjectStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QPen>
#include <QTextStream>
#include <QThread>

using namespace CyberSnapper;

namespace {

constexpr auto DemoUrl = "https://example.com";

bool drawDemoPage(const QString &path, bool changed) {
  QImage image(960, 620, QImage::Format_ARGB32_Premultiplied);
  image.fill(QColor("#f4f7fb"));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);

  painter.fillRect(0, 0, image.width(), 88, QColor("#07111f"));
  painter.setPen(QColor("#39d7ff"));
  painter.setFont(QFont("Sans Serif", 20, QFont::Bold));
  painter.drawText(QRect(38, 18, 430, 50), Qt::AlignVCenter, "NORTHSTAR / FIELD NOTES");
  painter.setPen(QColor("#d6e6f3"));
  painter.setFont(QFont("Sans Serif", 11));
  painter.drawText(QRect(660, 18, 260, 50), Qt::AlignRight | Qt::AlignVCenter,
                   "EXPLORE    STORIES    JOURNAL");

  painter.setPen(QColor("#13253a"));
  painter.setFont(QFont("Sans Serif", 33, QFont::Bold));
  painter.drawText(QRect(52, 126, 520, 100), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                   changed ? "Gear for the next horizon." : "Built for the next horizon.");
  painter.setPen(QColor("#52677c"));
  painter.setFont(QFont("Sans Serif", 14));
  painter.drawText(QRect(54, 222, 520, 70), Qt::AlignLeft | Qt::TextWordWrap,
                   "Field-tested essentials, captured across every viewport and checked against a trusted visual baseline.");

  const QColor accent = changed ? QColor("#925cff") : QColor("#0bbfe9");
  painter.setBrush(accent);
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(QRect(54, 308, 205, 52), 9, 9);
  painter.setPen(Qt::white);
  painter.setFont(QFont("Sans Serif", 13, QFont::DemiBold));
  painter.drawText(QRect(54, 308, 205, 52), Qt::AlignCenter,
                   changed ? "VIEW NEW COLLECTION" : "EXPLORE COLLECTION");

  painter.setBrush(QColor("#dce8f2"));
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(QRect(610, 126, 292, 250), 24, 24);
  painter.setBrush(QColor("#0b1d32"));
  painter.drawRoundedRect(QRect(668, 165, 176, 170), 16, 16);
  painter.setBrush(accent);
  painter.drawEllipse(QPoint(756, 250), changed ? 62 : 52, changed ? 62 : 52);
  painter.setBrush(QColor("#f4f7fb"));
  painter.drawEllipse(QPoint(756, 250), 28, 28);

  const QList<QString> labels{"DESKTOP 1440", "TABLET 768", "MOBILE 390"};
  for (int index = 0; index < labels.size(); ++index) {
    const QRect card(54 + index * 292, 425, 260, 130);
    painter.setBrush(Qt::white);
    painter.setPen(QPen(QColor("#d4e1ec"), 2));
    painter.drawRoundedRect(card, 14, 14);
    painter.setPen(QColor("#13253a"));
    painter.setFont(QFont("Sans Serif", 12, QFont::Bold));
    painter.drawText(card.adjusted(18, 16, -18, -70), Qt::AlignLeft | Qt::AlignVCenter, labels.at(index));
    painter.setPen(QColor("#6d8194"));
    painter.setFont(QFont("Sans Serif", 10));
    painter.drawText(card.adjusted(18, 58, -18, -14), Qt::AlignLeft | Qt::TextWordWrap,
                     index == 2 && changed ? "Navigation wraps cleanly\nTouch target updated" : "Layout stable\nBaseline matched");
  }
  painter.end();
  QDir().mkpath(QFileInfo(path).absolutePath());
  return image.save(path, "PNG");
}

bool drawDiff(const QString &path) {
  QImage image(960, 620, QImage::Format_ARGB32_Premultiplied);
  image.fill(QColor("#06101d"));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor("#ff4fd8"), 6));
  painter.setBrush(QColor(255, 79, 216, 42));
  painter.drawRoundedRect(QRect(46, 120, 535, 250), 18, 18);
  painter.setPen(QPen(QColor("#39d7ff"), 6));
  painter.setBrush(QColor(57, 215, 255, 38));
  painter.drawRoundedRect(QRect(630, 145, 252, 210), 18, 18);
  painter.setPen(QColor("#d7f7ff"));
  painter.setFont(QFont("Sans Serif", 22, QFont::Bold));
  painter.drawText(QRect(0, 470, 960, 50), Qt::AlignCenter, "2.437% VISUAL CHANGE");
  painter.setPen(QColor("#7f9aad"));
  painter.setFont(QFont("Sans Serif", 12));
  painter.drawText(QRect(0, 520, 960, 36), Qt::AlignCenter, "Headline, action color, and product art changed");
  painter.end();
  QDir().mkpath(QFileInfo(path).absolutePath());
  return image.save(path, "PNG");
}

QJsonObject artifact(const QString &id, const QString &relativePath, const QString &viewport,
                     const QString &engine = "chromium", const QString &status = "succeeded") {
  return {{"id", id}, {"url", DemoUrl}, {"engine", engine}, {"viewportId", viewport.toLower()},
          {"viewportName", viewport}, {"captureMode", "fullPage"}, {"format", "png"},
          {"relativePath", relativePath}, {"width", viewport == "Mobile" ? 780 : 1440},
          {"height", viewport == "Mobile" ? 1688 : 900}, {"sha256", id + "-fixture"},
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
  request.urls = {DemoUrl, "https://example.org/pricing"};
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
  if (!store.create(root, "Visual QA Demo", &error)) return fail(error);
  CaptureProfile profile = defaultProfile();
  profile.name = "Responsive Visual QA";
  profile.viewports = {{"desktop", "Desktop", 1440, 900, 1.0, false, true},
                       {"tablet", "Tablet", 768, 1024, 1.0, true, true},
                       {"mobile", "Mobile", 390, 844, 2.0, true, true}};
  profile.engines = {"chromium", "firefox"};
  profile.formats = {"png", "webp"};
  profile.concurrency = 2;
  profile.blockPopups = true;
  profile.comparisonEnabled = true;
  profile.comparisonIgnoreSelectors = {".timestamp", ".rotating-promo"};
  if (!store.saveProfile(profile, &error)) return fail(error);

  const QString baseRelative = "captures/showcase/baseline.png";
  const QString currentRelative = "captures/showcase/current.png";
  const QString diffRelative = "captures/showcase/difference.png";
  if (!drawDemoPage(QDir(root).filePath(baseRelative), false) ||
      !drawDemoPage(QDir(root).filePath(currentRelative), true) ||
      !drawDiff(QDir(root).filePath(diffRelative))) return fail("could not draw fixture images");

  if (!addJob(store, profile, "job-api-failure", "api", "failed", 0, 1, {}, "DNS lookup failed")) return fail("could not add API job");
  if (!addJob(store, profile, "job-weekday-schedule", "schedule:weekday", "succeeded", 12, 0,
              {artifact("artifact-schedule", currentRelative, "Tablet", "firefox")})) return fail("could not add scheduled job");
  if (!addJob(store, profile, "job-partial", "gui", "partial", 5, 1,
              {artifact("artifact-partial", currentRelative, "Mobile"),
               artifact("artifact-timeout", "captures/showcase/missing.png", "Desktop", "firefox", "failed")},
              "One Firefox target timed out")) return fail("could not add partial job");
  if (!addJob(store, profile, "job-baseline", "gui", "succeeded", 1, 0,
              {artifact("artifact-baseline", baseRelative, "Desktop")})) return fail("could not add baseline job");
  if (!addJob(store, profile, "job-visual-change", "schedule:release-watch", "succeeded", 1, 0,
              {artifact("artifact-current", currentRelative, "Desktop")})) return fail("could not add comparison job");

  const QString key = QString(DemoUrl) + "|chromium|desktop|fullPage|png";
  if (!store.setBaseline(key, "artifact-baseline", baseRelative)) return fail("could not set baseline");
  if (!store.insertComparison({{"id", "comparison-release"}, {"jobId", "job-visual-change"},
                               {"comparisonKey", key}, {"baselineArtifactId", "artifact-baseline"},
                               {"currentArtifactId", "artifact-current"}, {"status", "changed"},
                               {"mismatchRatio", 0.02437}, {"diffRelativePath", diffRelative},
                               {"createdAt", utcNow()}})) return fail("could not add comparison");

  const QJsonArray urls{DemoUrl, "https://example.org/pricing"};
  const QList<QJsonObject> schedules{
      {{"id", "schedule-release"}, {"name", "Release candidate watch"}, {"enabled", true},
       {"profileId", "default"}, {"urls", urls},
       {"recurrence", QJsonObject{{"type", "weekly"}, {"weekdays", QJsonArray{1, 2, 3, 4, 5}},
                                   {"time", "08:30"}, {"timeZone", "America/Los_Angeles"}}},
       {"lastRun", "2026-08-14T15:30:00.000Z"}, {"nextRun", "2030-01-02T16:30:00.000Z"}, {"lastStatus", "succeeded"}},
      {{"id", "schedule-nightly"}, {"name", "Nightly visual archive"}, {"enabled", true},
       {"profileId", "default"}, {"urls", urls},
       {"recurrence", QJsonObject{{"type", "daily"}, {"time", "02:00"}, {"timeZone", "UTC"}}},
       {"lastRun", "2026-08-14T02:00:00.000Z"}, {"nextRun", "2030-01-03T02:00:00.000Z"}, {"lastStatus", "succeeded"}},
      {{"id", "schedule-monthly"}, {"name", "Monthly compliance archive"}, {"enabled", false},
       {"profileId", "default"}, {"urls", QJsonArray{DemoUrl}},
       {"recurrence", QJsonObject{{"type", "monthly"}, {"day", 1}, {"time", "09:00"}, {"timeZone", "UTC"}}},
       {"lastRun", "2026-08-01T09:00:00.000Z"}, {"nextRun", "2030-02-01T09:00:00.000Z"}, {"lastStatus", "succeeded"}},
  };
  for (const auto &schedule : schedules) if (!store.upsertSchedule(schedule, &error)) return fail(error);
  QTextStream(stdout) << root << '\n';
  return 0;
}
