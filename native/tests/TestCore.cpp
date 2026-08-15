#include "core/Models.h"
#include "core/ProjectStore.h"
#include "core/Scheduler.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace CyberSnapper;

class TestCore final : public QObject {
  Q_OBJECT
private slots:
  void profileNormalization();
  void projectPersistence();
  void strictProjectLifecycle();
  void transactionalWorkerEvents();
  void schemaThreeMigration();
  void targetSetsAndReviewWorkflow();
  void intervalSchedule();
  void dailySchedule();
  void weeklyMonthlyAndOnceSchedules();
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
  QVERIFY2(store.create(temporary.path(), "Test Project", &error), qPrintable(error));
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

void TestCore::strictProjectLifecycle() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString missing = temporary.filePath("moved-project");
  ProjectStore missingStore;
  QString error;
  QVERIFY(!missingStore.open(missing, &error));
  QVERIFY(!QFileInfo::exists(missing));
  QVERIFY(error.contains("manifest", Qt::CaseInsensitive) || error.contains("project", Qt::CaseInsensitive));

  const QString projectRoot = temporary.filePath("project");
  QVERIFY(QDir().mkpath(projectRoot));
  {
    ProjectStore created;
    QVERIFY2(created.create(projectRoot, "Persistent", &error), qPrintable(error));
    QVERIFY(created.setAllowLocalhost(true, &error));
  }
  ProjectStore reopened;
  QVERIFY2(reopened.open(projectRoot, &error), qPrintable(error));
  QCOMPARE(reopened.projectName(), QString("Persistent"));
  QVERIFY(reopened.allowLocalhost());

  const QString nonempty = temporary.filePath("nonempty");
  QVERIFY(QDir().mkpath(nonempty));
  QFile marker(QDir(nonempty).filePath("keep.txt"));
  QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("keep"); marker.close();
  ProjectStore refused;
  QVERIFY(!refused.create(nonempty, "No", &error));
  QVERIFY(QFileInfo::exists(marker.fileName()));
}

void TestCore::transactionalWorkerEvents() {
  QTemporaryDir temporary;
  ProjectStore store;
  QString error;
  QVERIFY2(store.create(temporary.path(), "Events", &error), qPrintable(error));
  JobRequest request;
  request.id = newId(); request.urls = {"https://example.com"}; request.profile = defaultProfile();
  QVERIFY(store.insertJob(request, &error));
  QVERIFY(store.applyWorkerEvent(request.id, {{"sequence", 2}, {"type", "job_started"}, {"timestamp", utcNow()}}, &error));
  const QString artifactId = newId();
  const QJsonObject artifact{{"id", artifactId}, {"jobId", request.id}, {"url", "https://example.com"},
      {"engine", "chromium"}, {"viewportId", "desktop"}, {"viewportName", "Desktop"},
      {"captureMode", "fullPage"}, {"format", "png"}, {"relativePath", "captures/result.png"},
      {"status", "succeeded"}, {"createdAt", utcNow()}};
  QVERIFY(store.applyWorkerEvent(request.id, {{"sequence", 3}, {"type", "artifact_completed"},
                                               {"timestamp", utcNow()}, {"artifact", artifact}}, &error));
  const QJsonObject job = store.job(request.id);
  QCOMPARE(job.value("status").toString(), QString("running"));
  QCOMPARE(job.value("completedArtifacts").toInt(), 1);
  QCOMPARE(store.events(request.id).size(), 3);
  QCOMPARE(store.artifact(artifactId).value("status").toString(), QString("succeeded"));
  QVERIFY(!store.applyWorkerEvent(request.id, {{"sequence", 3}, {"type", "job_failed"},
                                                {"timestamp", utcNow()}}, &error));
  QCOMPARE(store.job(request.id).value("status").toString(), QString("running"));
  QCOMPARE(store.events(request.id).size(), 3);
}

void TestCore::schemaThreeMigration() {
  QTemporaryDir temporary;
  QString comparisonId;
  {
    ProjectStore store;
    QString error;
    QVERIFY2(store.create(temporary.path(), "Migration", &error), qPrintable(error));
    JobRequest request;
    request.id = newId(); request.urls = {"https://example.com"}; request.profile = defaultProfile();
    QVERIFY(store.insertJob(request, &error));
    const QString artifactId = newId();
    QVERIFY(store.insertArtifact(request.id, {{"id", artifactId}, {"url", "https://example.com"},
        {"targetId", "home"}, {"targetName", "Homepage"}, {"targetSetId", "production"}, {"targetSetName", "Production"},
        {"engine", "chromium"}, {"viewportId", "desktop"}, {"viewportName", "Desktop"},
        {"captureMode", "fullPage"}, {"format", "png"}, {"relativePath", "captures/current.png"},
        {"status", "succeeded"}, {"createdAt", utcNow()}}));
    comparisonId = newId();
    QVERIFY(store.insertComparison({{"id", comparisonId}, {"jobId", request.id}, {"comparisonKey", "key"},
        {"currentArtifactId", artifactId}, {"status", "changed"}, {"createdAt", utcNow()}}));
  }

  const QString connectionName = "schema-three-fixture";
  {
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    database.setDatabaseName(temporary.filePath(".cybersnapper/project.sqlite"));
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec("UPDATE metadata SET value='3' WHERE key='schemaVersion'"));
    QVERIFY(query.exec("DROP TABLE comparison_reviews"));
    QVERIFY(query.exec("DROP TABLE targets"));
    QVERIFY(query.exec("DROP TABLE target_sets"));
    QVERIFY(query.exec("UPDATE artifacts SET target_id='',target_name='',target_set_id='',target_set_name=''"));
    QVERIFY(query.exec("UPDATE comparisons SET target_url='',target_id='',target_name='',target_set_id='',target_set_name='',engine='',viewport_id='',viewport_name='',capture_mode='',format=''"));
    database.close();
  }
  QSqlDatabase::removeDatabase(connectionName);

  ProjectStore migrated;
  QString error;
  QVERIFY2(migrated.open(temporary.path(), &error), qPrintable(error));
  const QJsonObject comparison = migrated.comparison(comparisonId);
  QCOMPARE(comparison.value("targetId").toString(), QString("home"));
  QCOMPARE(comparison.value("targetSetName").toString(), QString("Production"));
  const QJsonObject saved = migrated.saveTargetSet({{"name", "Recreated"}, {"targets", QJsonArray{}}}, &error);
  QVERIFY2(!saved.isEmpty(), qPrintable(error));
}

void TestCore::targetSetsAndReviewWorkflow() {
  QTemporaryDir temporary;
  ProjectStore store;
  QString error;
  QVERIFY2(store.create(temporary.path(), "Review", &error), qPrintable(error));
  const QJsonObject saved = store.saveTargetSet({{"name", "Production"}, {"description", "Public pages"},
      {"targets", QJsonArray{QJsonObject{{"id", "home"}, {"label", "Home"}, {"url", "https://example.com"}, {"enabled", true}},
                             QJsonObject{{"id", "pricing"}, {"label", "Pricing"}, {"url", "https://example.com/pricing"}, {"enabled", false}}}}}, &error);
  QVERIFY2(!saved.isEmpty(), qPrintable(error));
  QCOMPARE(store.targetSets().size(), 1);
  QCOMPARE(store.targetSet(saved.value("id").toString()).value("targets").toArray().size(), 2);

  JobRequest request;
  request.id = newId(); request.urls = {"https://example.com"}; request.profile = defaultProfile();
  QVERIFY(store.insertJob(request, &error));
  const QString artifactId = newId();
  QVERIFY(store.insertArtifact(request.id, {{"id", artifactId}, {"url", "https://example.com"},
      {"targetId", "home"}, {"targetName", "Home"}, {"targetSetId", saved.value("id")}, {"targetSetName", "Production"},
      {"engine", "chromium"}, {"viewportId", "desktop"}, {"viewportName", "Desktop"},
      {"captureMode", "fullPage"}, {"format", "png"}, {"relativePath", "captures/current.png"},
      {"status", "succeeded"}, {"createdAt", utcNow()}}));
  const QString comparisonId = newId();
  QVERIFY(store.insertComparison({{"id", comparisonId}, {"jobId", request.id},
      {"comparisonKey", "https://example.com|chromium|desktop|fullPage|png"},
      {"currentArtifactId", artifactId}, {"status", "missing_baseline"}, {"url", "https://example.com"},
      {"targetId", "home"}, {"targetName", "Home"}, {"targetSetId", saved.value("id")},
      {"targetSetName", "Production"}, {"engine", "chromium"}, {"viewportId", "desktop"},
      {"viewportName", "Desktop"}, {"captureMode", "fullPage"}, {"format", "png"},
      {"analysisWidth", 1440}, {"analysisHeight", 900}, {"analysisScale", 1.0}, {"createdAt", utcNow()}}));
  QCOMPARE(store.comparison(comparisonId).value("review").toObject().value("status").toString(), QString("unreviewed"));
  const QJsonObject ignored = store.setComparisonReview(comparisonId, "ignored", "Expected animation", 0, &error);
  QVERIFY2(!ignored.isEmpty(), qPrintable(error));
  QCOMPARE(ignored.value("review").toObject().value("revision").toInt(), 1);
  QVERIFY(store.setComparisonReview(comparisonId, "unreviewed", "Expected animation", 0, &error).isEmpty());
  const QJsonObject accepted = store.acceptComparison(comparisonId, "baselines/current.png", "Approved", 1, false, &error);
  QVERIFY2(!accepted.isEmpty(), qPrintable(error));
  QCOMPARE(store.baseline(accepted.value("comparisonKey").toString()).value("artifactId").toString(), artifactId);
  QCOMPARE(store.dashboard().value("needsReview").toInt(), 0);

  QJsonObject schedule{{"id", "schedule"}, {"name", "Daily"}, {"enabled", true}, {"profileId", "default"},
                       {"targetSetId", saved.value("id")}, {"urls", QJsonArray{}},
                       {"recurrence", QJsonObject{{"type", "daily"}, {"time", "09:00"}, {"timeZone", "UTC"}}},
                       {"nextRun", "2030-01-01T09:00:00.000Z"}};
  QVERIFY(store.upsertSchedule(schedule, &error));
  QVERIFY(!store.removeTargetSet(saved.value("id").toString(), &error));
  QVERIFY(error.contains("Daily"));
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

void TestCore::weeklyMonthlyAndOnceSchedules() {
  const QDateTime friday(QDate(2026, 8, 14), QTime(18, 0), QTimeZone::UTC);
  QCOMPARE(Scheduler::nextOccurrence({{"type", "weekly"}, {"time", "09:30"},
                                      {"daysOfWeek", QJsonArray{1, 3}}, {"timeZone", "UTC"}}, friday),
           QDateTime(QDate(2026, 8, 17), QTime(9, 30), QTimeZone::UTC));
  QCOMPARE(Scheduler::nextOccurrence({{"type", "monthly"}, {"time", "08:00"},
                                      {"day", "last"}, {"timeZone", "UTC"}}, friday),
           QDateTime(QDate(2026, 8, 31), QTime(8, 0), QTimeZone::UTC));
  const QDateTime once = friday.addSecs(3600);
  QCOMPARE(Scheduler::nextOccurrence({{"type", "once"}, {"at", once.toString(Qt::ISODate)}}, friday), once);
  QVERIFY(!Scheduler::nextOccurrence({{"type", "once"}, {"at", friday.toString(Qt::ISODate)}}, friday).isValid());
}

QTEST_GUILESS_MAIN(TestCore)
#include "TestCore.moc"
