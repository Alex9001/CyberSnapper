#include "core/Models.h"
#include "core/BrowserManager.h"
#include "core/ProjectStore.h"
#include "core/ContentRulesets.h"
#include "core/SubscriptionRefresher.h"
#include "core/RestServer.h"
#include "core/Scheduler.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
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
  void restServerReportsRequestedPort();
  void intervalSchedule();
  void dailySchedule();
  void weeklyMonthlyAndOnceSchedules();
  void contentRulesetCrud();
  void rulesSnapshotBuilderAndJobContract();
  void rulesSnapshotUsesCachedSubscriptions();
  void browserManagerQueuesVerifiesAndCancels();
};

void TestCore::browserManagerQueuesVerifiesAndCancels() {
  const QByteArray oldWorker = qgetenv("CYBERSNAPPER_WORKER_ENTRY");
  const QByteArray oldNode = qgetenv("CYBERSNAPPER_NODE");
  const QByteArray oldCache = qgetenv("CYBERSNAPPER_BROWSER_CACHE");
  const QString node = QStandardPaths::findExecutable("node");
  QVERIFY2(!node.isEmpty(), "Node is required for the browser-manager fixture");
  const QString fixture = QStringLiteral(CYBERSNAPPER_SOURCE_ROOT
                                          "/native/tests/fixtures/browser-worker.cjs");
  QVERIFY(QFileInfo::exists(fixture));
  QTemporaryDir cache;
  QVERIFY(cache.isValid());
  qputenv("CYBERSNAPPER_WORKER_ENTRY", fixture.toUtf8());
  qputenv("CYBERSNAPPER_NODE", node.toUtf8());
  qputenv("CYBERSNAPPER_BROWSER_CACHE", cache.path().toUtf8());

  BrowserManager manager;
  QSignalSpy progress(&manager, &BrowserManager::progressPublished);
  QSignalSpy finished(&manager, &BrowserManager::finishedPublished);
  const QJsonObject chromium = manager.install("chromium");
  const QJsonObject duplicate = manager.install("chromium");
  QCOMPARE(duplicate.value("duplicate").toBool(), true);
  QCOMPARE(duplicate.value("installId"), chromium.value("installId"));
  const QJsonObject firefox = manager.install("firefox");
  QCOMPARE(manager.queuedCount(), 2);
  QCOMPARE(manager.cancel(firefox.value("installId").toString()).value("cancelled").toBool(), true);
  QCOMPARE(manager.task(firefox.value("installId").toString()).value("install").toObject()
               .value("state").toString(), QString("cancelled"));
  QTRY_VERIFY_WITH_TIMEOUT(finished.size() >= 2, 3000);
  QCOMPARE(manager.task(chromium.value("installId").toString()).value("install").toObject()
               .value("state").toString(), QString("ready"));
  QVERIFY(!progress.isEmpty());

  const QJsonObject webkit = manager.verify("webkit");
  const QString webkitId = webkit.value("installId").toString();
  QTRY_VERIFY_WITH_TIMEOUT(manager.task(webkitId).value("install").toObject()
                               .value("state").toString() == "verifying", 1000);
  QCOMPARE(manager.cancel(webkitId).value("cancelling").toBool(), true);
  QTRY_COMPARE_WITH_TIMEOUT(manager.task(webkitId).value("install").toObject()
                                .value("state").toString(), QString("cancelled"), 3000);

  const QJsonObject firefoxCheck = manager.verify("firefox");
  const QString firefoxCheckId = firefoxCheck.value("installId").toString();
  QTRY_COMPARE_WITH_TIMEOUT(manager.task(firefoxCheckId).value("install").toObject()
                                .value("state").toString(), QString("ready"), 3000);
  const QJsonArray firefoxLogs = manager.task(firefoxCheckId).value("install").toObject()
                                     .value("logs").toArray();
  for (const QJsonValue &line : firefoxLogs) {
    QVERIFY2(!line.toString().contains("browser_install_result"),
             "Terminal protocol JSON leaked into user-visible browser logs");
  }

  const QJsonObject failedWebkit = manager.install("webkit");
  const QString failedWebkitId = failedWebkit.value("installId").toString();
  QTRY_COMPARE_WITH_TIMEOUT(manager.task(failedWebkitId).value("install").toObject()
                                .value("state").toString(), QString("failed"), 3000);
  QVERIFY(!manager.hasPendingOperations());

  if (oldWorker.isNull()) qunsetenv("CYBERSNAPPER_WORKER_ENTRY");
  else qputenv("CYBERSNAPPER_WORKER_ENTRY", oldWorker);
  if (oldNode.isNull()) qunsetenv("CYBERSNAPPER_NODE");
  else qputenv("CYBERSNAPPER_NODE", oldNode);
  if (oldCache.isNull()) qunsetenv("CYBERSNAPPER_BROWSER_CACHE");
  else qputenv("CYBERSNAPPER_BROWSER_CACHE", oldCache);
}

void TestCore::profileNormalization() {
  const CaptureProfile profile = profileFromJson({{"id", "custom"}, {"name", "Custom"},
      {"concurrency", 999}, {"captureMode", "invalid"}, {"formats", QJsonArray{"png", "webp"}},
      {"presentation", QJsonObject{{"enabled", true}, {"scene", "unknown"}, {"frame", "laptop"},
          {"aspect", "wide"}, {"padding", "huge"}, {"shadow", "fog"}, {"solidColor", "red"}}},
      {"viewports", QJsonArray{QJsonObject{{"id", "tiny"}, {"name", "Tiny"}, {"width", 1}, {"height", 999999}}}}});
  QCOMPARE(profile.id, QString("custom"));
  QCOMPARE(profile.concurrency, 10);
  QCOMPARE(profile.captureMode, QString("fullPage"));
  QCOMPARE(profile.viewports.first().width, 64);
  QCOMPARE(profile.viewports.first().height, 16384);
  QVERIFY(profile.presentation.enabled);
  QCOMPARE(profile.presentation.scene, QString("aurora"));
  QCOMPARE(profile.presentation.frame, QString("auto"));
  QCOMPARE(profile.presentation.aspect, QString("auto"));
  QCOMPARE(profile.presentation.padding, QString("balanced"));
  QCOMPARE(profile.presentation.shadow, QString("soft"));
  QCOMPARE(profile.presentation.solidColor, QString("#0B1220"));
  QCOMPARE(toJson(profile).value("presentation").toObject().value("enabled").toBool(), true);

  for (const QString &tabletFrame : {QString("lightTablet"), QString("darkTablet")}) {
    const CaptureProfile tabletProfile = profileFromJson({
        {"presentation", QJsonObject{{"frame", tabletFrame}}}});
    QCOMPARE(tabletProfile.presentation.frame, tabletFrame);
    QCOMPARE(toJson(tabletProfile).value("presentation").toObject().value("frame").toString(), tabletFrame);
  }
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

void TestCore::restServerReportsRequestedPort() {
  QTcpServer probe;
  QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
  const quint16 port = probe.serverPort();
  probe.close();

  RestServer server;
  QString error;
  QVERIFY2(server.start(port, QByteArray(64, '0'),
                        [](const QString &, const QJsonObject &) {
                          return QJsonObject{{"ok", true}};
                        }, &error), qPrintable(error));
  QVERIFY(server.isRunning());
  QCOMPARE(server.port(), port);
  server.stop();
  QVERIFY(!server.isRunning());
  QCOMPARE(server.port(), quint16(0));
}

void TestCore::contentRulesetCrud() {
  QTemporaryDir temporary;
  ProjectStore store;
  QString error;
  QVERIFY2(store.create(temporary.path(), "Rulesets", &error), qPrintable(error));

  const QJsonObject saved = store.saveContentRuleset({{"name", "My banners"},
      {"rulesText", "example.org##.cookie-banner\n! kept as-is"},
      {"actions", QJsonArray{QJsonObject{{"domains", QJsonArray{"example.org"}},
          {"selector", "button#reject-all"}, {"action", "click"}, {"delayMs", 250}}}}}, &error);
  QVERIFY2(!saved.isEmpty(), qPrintable(error));
  QVERIFY(!saved.value("id").toString().isEmpty());

  const QString id = saved.value("id").toString();
  QCOMPARE(store.contentRuleset(id).value("name").toString(), QString("My banners"));
  QCOMPARE(store.contentRuleset(id).value("kind").toString(), QString("custom"));
  QCOMPARE(store.contentRulesets().size(), 1);

  // Unsafe structured actions and oversized rule text are rejected.
  QString rejection;
  QVERIFY2(!store.saveContentRuleset({{"id", id}, {"name", "Renamed"}, {"autoUpdate", true},
      {"rulesText", "example.org##.cookie-banner"}}, &rejection).isEmpty(), qPrintable(rejection));
  QCOMPARE(store.contentRuleset(id).value("name").toString(), QString("Renamed"));
  QCOMPARE(store.contentRuleset(id).value("autoUpdate").toBool(), true);

  QVERIFY(store.saveContentRuleset({{"name", "Bad"}, {"actions", QJsonArray{QJsonObject{
      {"domains", QJsonArray{"example.org"}}, {"selector", "ok"},
      {"action", "eval"}, {"delayMs", 0}}}}}, &rejection).isEmpty());
  QVERIFY(store.saveContentRuleset({{"name", "Bad"},
      {"actions", QJsonArray{QJsonObject{{"domains", QJsonArray{}},
          {"selector", "ok"}, {"action", "click"}, {"delayMs", 0}}}}}, &rejection).isEmpty());
  QVERIFY(store.saveContentRuleset({{"name", "Bad"}}, &rejection).isEmpty());

  QVERIFY2(store.removeContentRuleset(id, &error), qPrintable(error));
  QVERIFY(store.contentRuleset(id).isEmpty());
}

void TestCore::rulesSnapshotBuilderAndJobContract() {
  QTemporaryDir temporary;
  ProjectStore store;
  QString error;
  QVERIFY2(store.create(temporary.path(), "Snapshots", &error), qPrintable(error));

  CaptureProfile profile = defaultProfile();
  QVERIFY(profile.contentBlocking.enabled);
  profile.contentBlocking.customRulesetIds.append(
      store.saveContentRuleset({{"name", "Custom"}, {"rulesText", "example.org##.banner"},
          {"actions", QJsonArray{QJsonObject{{"domains", QJsonArray{"example.org"}},
              {"selector", "button.accept"}, {"action", "hide"}, {"delayMs", 0}}}}}, &error)
          .value("id").toString());
  // A subscription id that has no cached list must only produce a warning.
  QVERIFY(profile.contentBlocking.subscriptionIds.contains("easylist-cookie"));

  RulesetReferenceInfo reference = ContentRulesets::buildSnapshot(&store, profile, &error);
  QVERIFY2(reference.enabled, qPrintable(error));
  QVERIFY(reference.digest.size() == 64);
  QVERIFY(reference.relativePath.startsWith(".cybersnapper/rulesets/"));
  QVERIFY(QFile::exists(temporary.filePath(reference.relativePath)));
  QVERIFY(!reference.warnings.isEmpty());
  QVERIFY(!reference.sources.isEmpty());

  // The snapshot envelope is exactly what the worker verifies.
  QFile snapshot(temporary.filePath(reference.relativePath));
  QVERIFY(snapshot.open(QIODevice::ReadOnly));
  const QJsonObject envelope =
      QJsonDocument::fromJson(snapshot.readAll()).object();
  snapshot.close();
  QCOMPARE(envelope.value("format").toInt(), 1);
  QCOMPARE(envelope.value("digest").toString(), reference.digest);
  const QByteArray payload = envelope.value("payload").toString().toUtf8();
  QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()),
           reference.digest);
  const QJsonObject parsed = QJsonDocument::fromJson(payload).object();
  QVERIFY(parsed.value("rulesText").toArray().contains(QJsonValue("example.org##.banner")));
  QCOMPARE(parsed.value("actions").toArray().size(), 1);

  // Identical source content must reuse the same content-addressed snapshot.
  // Wall-clock creation time belongs to the envelope, not the hashed payload.
  const RulesetReferenceInfo repeated = ContentRulesets::buildSnapshot(&store, profile, &error);
  QCOMPARE(repeated.digest, reference.digest);
  QCOMPARE(repeated.relativePath, reference.relativePath);

  // The job contract carries the snapshot as a nested ruleset object and
  // round-trips through the stored request JSON for recovery.
  JobRequest request;
  request.id = newId();
  request.projectId = store.projectId();
  request.projectRoot = store.root();
  request.urls = {"https://example.org"};
  request.profile = profile;
  request.rulesetDigest = reference.digest;
  request.rulesetRelativePath = reference.relativePath;
  request.rulesetSources = reference.sources;
  request.rulesetWarnings = reference.warnings;
  const QJsonObject serialized = toJson(request);
  const QJsonObject rulesetJson = serialized.value("ruleset").toObject();
  QCOMPARE(rulesetJson.value("digest").toString(), reference.digest);
  QCOMPARE(rulesetJson.value("relativePath").toString(), reference.relativePath);
  QVERIFY(serialized.value("rulesetDigest").isUndefined());
  QVERIFY2(store.insertJob(request, &error), qPrintable(error));
  const JobRequest restored =
      jobRequestFromJson(store.job(request.id).value("request").toObject());
  QCOMPARE(restored.rulesetDigest, reference.digest);
  QCOMPARE(restored.rulesetRelativePath, reference.relativePath);
  QVERIFY(restored.rulesetSources.keys().size() == reference.sources.keys().size());
  QVERIFY(restored.profile.contentBlocking.enabled);

  // Disabled content blocking produces no snapshot at all.
  CaptureProfile offProfile = defaultProfile();
  offProfile.contentBlocking.enabled = false;
  QVERIFY(!ContentRulesets::buildSnapshot(&store, offProfile, &error).enabled);
}

void TestCore::rulesSnapshotUsesCachedSubscriptions() {
  // ContentRulesets reads its cache through Paths::cacheDir(); point
  // XDG_CACHE_HOME at an isolated directory before touching it.
  QTemporaryDir cacheRoot;
  QVERIFY(cacheRoot.isValid());
  const QString previous = qEnvironmentVariable("XDG_CACHE_HOME");
  qputenv("XDG_CACHE_HOME", cacheRoot.path().toUtf8());
  // Resolve through the production path so the test follows whatever
  // platform-specific sublayout QStandardPaths applies.
  const QString rulesetCache = ContentRulesets::cacheDirectory();
  QVERIFY(QDir().mkpath(rulesetCache));
  const QByteArray listBody =
      "! EasyList Cookie fixture\n[Adblock Plus 2.0]\n||cdn.example^$script\nexample.org##.consent\n";
  QFile list(QDir(rulesetCache).filePath("easylist-cookie.txt"));
  QVERIFY(list.open(QIODevice::WriteOnly));
  QCOMPARE(list.write(listBody), qint64(listBody.size()));
  list.close();
  const QString digest =
      QString::fromLatin1(QCryptographicHash::hash(listBody, QCryptographicHash::Sha256).toHex());
  {
    QFile meta(QDir(rulesetCache).filePath("easylist-cookie.json"));
    QVERIFY(meta.open(QIODevice::WriteOnly));
    meta.write(QJsonDocument(QJsonObject{{"digest", digest}, {"ruleCount", 2},
                                         {"fetchedAt", "2026-01-01T00:00:00Z"}})
                   .toJson(QJsonDocument::Compact));
  }

  // The catalog status reflects the cached file.
  const QJsonObject status = subscriptionCacheMeta("easylist-cookie");
  QCOMPARE(status.value("downloaded").toBool(), true);
  QCOMPARE(status.value("ruleCount").toInt(), 2);
  QCOMPARE(status.value("digest").toString(), digest);

  QTemporaryDir temporary;
  ProjectStore store;
  QString error;
  QVERIFY2(store.create(temporary.path(), "Cached", &error), qPrintable(error));

  CaptureProfile profile = defaultProfile();
  profile.contentBlocking.customRulesetIds.clear();
  profile.contentBlocking.subscriptionIds = {"easylist-cookie"};

  RulesetReferenceInfo reference = ContentRulesets::buildSnapshot(&store, profile, &error);
  QVERIFY2(reference.enabled, qPrintable(error));
  QVERIFY(reference.warnings.isEmpty());
  const QJsonObject source = reference.sources.value("easylist-cookie").toObject();
  QCOMPARE(source.value("digest").toString(), digest);
  QCOMPARE(source.value("license").toString(), QString("CC BY-SA 3.0"));

  QFile snapshot(QDir(store.root()).filePath(reference.relativePath));
  QVERIFY(snapshot.open(QIODevice::ReadOnly));
  const QByteArray payloadBytes = QJsonDocument::fromJson(snapshot.readAll())
                                      .object().value("payload").toString().toUtf8();
  snapshot.close();
  const QJsonObject payload = QJsonDocument::fromJson(payloadBytes).object();
  QVERIFY(payload.value("rulesText").toArray().contains(QJsonValue("||cdn.example^$script")));
  QCOMPARE(payload.value("actions").toArray().size(), 0);
  snapshot.close();

  if (previous.isEmpty()) qunsetenv("XDG_CACHE_HOME");
  else qputenv("XDG_CACHE_HOME", previous.toUtf8());
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
