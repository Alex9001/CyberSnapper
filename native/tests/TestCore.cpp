#include "core/Models.h"
#include "core/ProjectStore.h"
#include "core/Scheduler.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace CyberSnapper;

class TestCore final : public QObject {
  Q_OBJECT
private slots:
  void profileNormalization();
  void projectPersistence();
  void intervalSchedule();
  void dailySchedule();
};

void TestCore::profileNormalization() {
  const CaptureProfile profile = profileFromJson({{"id", "custom"}, {"name", "Custom"},
      {"concurrency", 999}, {"captureMode", "invalid"}, {"formats", QJsonArray{"png", "webp"}},
      {"viewports", QJsonArray{QJsonObject{{"id", "tiny"}, {"name", "Tiny"}, {"width", 1}, {"height", 999999}}}}});
  QCOMPARE(profile.id, QString("custom"));
  QCOMPARE(profile.concurrency, 10);
  QCOMPARE(profile.captureMode, QString("fullPage"));
  QCOMPARE(profile.viewports.first().width, 64);
  QCOMPARE(profile.viewports.first().height, 16384);
}

void TestCore::projectPersistence() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ProjectStore store;
  QString error;
  QVERIFY2(store.open(temporary.path(), "Test Project", &error), qPrintable(error));
  QVERIFY(!store.projectId().isEmpty());
  QCOMPARE(store.projectName(), QString("Test Project"));
  QVERIFY(QFile::exists(temporary.filePath("project.cybersnapper.json")));
  QVERIFY(QFile::exists(temporary.filePath(".cybersnapper/project.sqlite")));
  QCOMPARE(store.profiles().size(), 1);

  JobRequest request;
  request.id = newId();
  request.projectId = store.projectId();
  request.projectRoot = store.root();
  request.profileId = "default";
  request.urls = {"https://example.com"};
  request.profile = defaultProfile();
  QVERIFY2(store.insertJob(request, &error), qPrintable(error));
  QVERIFY(store.appendEvent(request.id, {{"sequence", 1}, {"type", "job_queued"}, {"timestamp", utcNow()}}));
  const QString artifactId = newId();
  QVERIFY(store.insertArtifact(request.id, {{"id", artifactId}, {"url", "https://example.com"},
      {"engine", "chromium"}, {"viewportId", "desktop"}, {"viewportName", "Desktop"},
      {"captureMode", "fullPage"}, {"format", "png"}, {"relativePath", "captures/test.png"},
      {"status", "succeeded"}, {"createdAt", utcNow()}}));
  QCOMPARE(store.jobs().size(), 1);
  QCOMPARE(store.events(request.id).size(), 1);
  QCOMPARE(store.artifact(artifactId).value("relativePath").toString(), QString("captures/test.png"));
  QVERIFY(store.setBaseline("key", artifactId));
  QCOMPARE(store.baseline("key").value("artifactId").toString(), artifactId);
}

void TestCore::intervalSchedule() {
  const QDateTime start(QDate(2026, 8, 14), QTime(12, 0), QTimeZone::UTC);
  const QDateTime next = Scheduler::nextOccurrence({{"type", "interval"}, {"minutes", 30}}, start);
  QCOMPARE(next, start.addSecs(1800));
}

void TestCore::dailySchedule() {
  const QDateTime start(QDate(2026, 8, 14), QTime(18, 0), QTimeZone::UTC);
  const QDateTime next = Scheduler::nextOccurrence({{"type", "daily"}, {"time", "09:00"},
                                                     {"timeZone", "UTC"}}, start);
  QCOMPARE(next, QDateTime(QDate(2026, 8, 15), QTime(9, 0), QTimeZone::UTC));
}

QTEST_GUILESS_MAIN(TestCore)
#include "TestCore.moc"
