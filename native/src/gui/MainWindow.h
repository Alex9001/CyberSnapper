#pragma once

#include "core/Rpc.h"

#include <QJsonArray>
#include <QHash>
#include <QMainWindow>
#include <QSet>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QTextEdit;
class QTreeWidget;
class QPushButton;
class QSplitter;
class QTabWidget;

namespace CyberSnapper {

class ImageCanvas;
class OverlayCanvas;

class MainWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;
  void connectToAgent();
  bool prepareScreenshotScene(const QString &scene);

private:
  RpcClient m_rpc;
  QString m_projectId;
  QString m_loadedProfileId;
  QJsonArray m_projects;
  QJsonArray m_profiles;
  QJsonArray m_jobsCache;
  QJsonArray m_schedulesCache;
  QJsonArray m_comparisonsCache;
  QSet<QString> m_installedBrowsers;
  bool m_loadingProfile = false;
  bool m_profileDirty = false;
  bool m_refreshPending = false;
  bool m_reconnectPending = false;

  QLabel *m_connectionStatus = nullptr;
  QComboBox *m_projectCombo = nullptr;
  QComboBox *m_profileCombo = nullptr;
  QTextEdit *m_urls = nullptr;
  QComboBox *m_captureMode = nullptr;
  QLineEdit *m_elementSelector = nullptr;
  QCheckBox *m_chromium = nullptr;
  QCheckBox *m_firefox = nullptr;
  QCheckBox *m_webkit = nullptr;
  QCheckBox *m_png = nullptr;
  QCheckBox *m_webp = nullptr;
  QCheckBox *m_avif = nullptr;
  QCheckBox *m_pdf = nullptr;
  QDoubleSpinBox *m_initialDelay = nullptr;
  QDoubleSpinBox *m_scrollDelay = nullptr;
  QDoubleSpinBox *m_finalDelay = nullptr;
  QSpinBox *m_concurrency = nullptr;
  QCheckBox *m_blockPopups = nullptr;
  QLineEdit *m_waitSelector = nullptr;
  QLineEdit *m_hideSelectors = nullptr;
  QCheckBox *m_comparisonEnabled = nullptr;
  QDoubleSpinBox *m_pixelThreshold = nullptr;
  QDoubleSpinBox *m_mismatchThreshold = nullptr;
  QLineEdit *m_comparisonIgnoreSelectors = nullptr;
  QTableWidget *m_viewports = nullptr;
  QLabel *m_capturePlan = nullptr;
  QLabel *m_profileState = nullptr;
  QPushButton *m_startCapture = nullptr;
  QPushButton *m_saveProfile = nullptr;
  QPushButton *m_revertProfile = nullptr;
  QTreeWidget *m_activeJobs = nullptr;
  QTableWidget *m_history = nullptr;
  QLineEdit *m_historySearch = nullptr;
  QComboBox *m_historyStatus = nullptr;
  QComboBox *m_historySource = nullptr;
  QLabel *m_jobSummary = nullptr;
  QTableWidget *m_artifacts = nullptr;
  QTableWidget *m_comparisons = nullptr;
  QTableWidget *m_baselines = nullptr;
  ImageCanvas *m_baselineImage = nullptr;
  ImageCanvas *m_currentImage = nullptr;
  ImageCanvas *m_diffImage = nullptr;
  OverlayCanvas *m_overlayImage = nullptr;
  QTableWidget *m_schedules = nullptr;
  QCheckBox *m_apiEnabled = nullptr;
  QCheckBox *m_allowLocalhost = nullptr;
  QCheckBox *m_launchAtLogin = nullptr;
  QLabel *m_apiStatus = nullptr;
  QLabel *m_workerStatus = nullptr;
  QHash<QString, QLabel *> m_browserStatuses;
  QSpinBox *m_maximumJobs = nullptr;
  QTabWidget *m_tabs = nullptr;
  QSplitter *m_captureVertical = nullptr;
  QSplitter *m_captureColumns = nullptr;
  QSplitter *m_historySplit = nullptr;
  QSplitter *m_compareSplit = nullptr;

  void buildUi();
  QWidget *buildCapturePage();
  QWidget *buildHistoryPage();
  QWidget *buildComparePage();
  QWidget *buildSchedulesPage();
  QWidget *buildSettingsPage();
  QWidget *buildHelpPage();
  void showAbout();
  void rpcCall(const QString &method, const QJsonObject &params,
               std::function<void(const QJsonObject &)> success = {});
  void refreshAll();
  void refreshProjects();
  void refreshProfiles();
  void refreshJobs();
  void refreshSchedules();
  void refreshSettings();
  void refreshComparisons();
  void refreshBaselines();
  void scheduleRefresh();
  void submitCapture();
  QJsonObject captureProfile() const;
  void loadSelectedProfile();
  void markProfileDirty();
  void updateCapturePlan();
  void saveCurrentProfile();
  void openProfileManager();
  void applyHistoryFilters();
  void showSelectedComparison();
  void restoreUiState();
  void saveUiState() const;
  void showFirstRun();
  QString selectedJobId() const;
  QString selectedArtifactId() const;
  QString selectedScheduleId() const;
  void showJobDetails();
  void openSelectedArtifact();
  void setSelectedArtifactAsBaseline();
  void createSchedule();
  void editSchedule(const QJsonObject &schedule);
  void promptForAutostart();
};

} // namespace CyberSnapper
