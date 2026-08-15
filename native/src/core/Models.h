#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CyberSnapper {

struct Viewport {
  QString id;
  QString name;
  int width = 1920;
  int height = 1080;
  double deviceScaleFactor = 1.0;
  bool mobile = false;
  bool enabled = true;
};

struct PresentationSettings {
  bool enabled = false;
  QString scene{"aurora"};
  QString frame{"auto"};
  QString aspect{"auto"};
  QString padding{"balanced"};
  QString shadow{"soft"};
  QString solidColor{"#0B1220"};
};

struct CaptureProfile {
  QString id;
  QString name;
  QVector<Viewport> viewports;
  QStringList engines{"chromium"};
  QStringList formats{"png"};
  QString captureMode{"fullPage"};
  QString elementSelector;
  double initialDelay = 1.5;
  double scrollDelay = 1.8;
  double finalDelay = 1.0;
  int concurrency = 1;
  int navigationTimeoutSeconds = 60;
  int selectorTimeoutSeconds = 30;
  int maxScrollSeconds = 120;
  int maxPageHeight = 100000;
  bool blockPopups = false;
  bool stripWhitespace = true;
  QStringList blocklist;
  QStringList hideSelectors;
  QString waitForSelector;
  QString namingTemplate{"{hostname}-{preset}"};
  QString collisionPolicy{"version"};
  int webpQuality = 80;
  int avifQuality = 50;
  QString pdfFormat{"A4"};
  bool pdfLandscape = false;
  QString pdfMargin{"0"};
  bool comparisonEnabled = false;
  double pixelThreshold = 0.10;
  double mismatchThreshold = 0.001;
  QStringList comparisonIgnoreSelectors;
  PresentationSettings presentation;
};

struct CaptureTarget {
  QString id;
  QString name;
  QString url;
  QString targetSetId;
  QString targetSetName;
  bool enabled = true;
};

struct JobRequest {
  QString id;
  QString projectId;
  QString projectRoot;
  QString profileId;
  QString source{"gui"};
  QStringList urls;
  QString targetSetId;
  QVector<CaptureTarget> targets;
  CaptureProfile profile;
  QJsonObject baselines;
  bool allowLocalhost = false;
};

struct JobRecord {
  QString id;
  QString projectId;
  QString status;
  QString source;
  QString createdAt;
  QString startedAt;
  QString finishedAt;
  QString error;
  int completedArtifacts = 0;
  int failedArtifacts = 0;
  QJsonObject request;
};

CaptureProfile defaultProfile();
QJsonObject toJson(const Viewport &viewport);
Viewport viewportFromJson(const QJsonObject &object);
QJsonObject toJson(const CaptureProfile &profile);
CaptureProfile profileFromJson(const QJsonObject &object);
QJsonObject toJson(const CaptureTarget &target);
CaptureTarget captureTargetFromJson(const QJsonObject &object);
QJsonObject toJson(const JobRequest &request);
JobRequest jobRequestFromJson(const QJsonObject &object);
QJsonObject toJson(const JobRecord &record);

QString utcNow();
QString newId();
QString normalizedJobStatus(const QString &status);

} // namespace CyberSnapper
