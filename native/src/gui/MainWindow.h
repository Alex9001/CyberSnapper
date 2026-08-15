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
class QListWidget;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QTextEdit;
class QTreeWidget;
class QPushButton;
class QSplitter;
class QTabWidget;
class QToolBar;
class QAction;
class QMenu;

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
  QJsonArray m_targetSetsCache;
  QSet<QString> m_installedBrowsers;
  bool m_loadingProfile = false;
  bool m_profileDirty = false;
  bool m_refreshPending = false;
  bool m_reconnectPending = false;

  QLabel *m_connectionStatus = nullptr;
  QComboBox *m_projectCombo = nullptr;
  QComboBox *m_profileCombo = nullptr;
  QComboBox *m_targetSource = nullptr;
  QComboBox *m_captureTargetSet = nullptr;
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
  QWidget *m_activeJobsGroup = nullptr;
  QPushButton *m_cancelActiveJob = nullptr;
  QTableWidget *m_history = nullptr;
  QLineEdit *m_historySearch = nullptr;
  QComboBox *m_historyStatus = nullptr;
  QComboBox *m_historySource = nullptr;
  QLabel *m_jobSummary = nullptr;
  QTableWidget *m_artifacts = nullptr;
  QTableWidget *m_comparisons = nullptr;
  QLineEdit *m_reviewSearch = nullptr;
  QComboBox *m_reviewFilter = nullptr;
  QLabel *m_reviewSummary = nullptr;
  QTextEdit *m_reviewNote = nullptr;
  QPushButton *m_reviewAccept = nullptr;
  QPushButton *m_reviewIgnore = nullptr;
  QPushButton *m_reviewReset = nullptr;
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
  QLabel *m_dashboardNeedsReview = nullptr;
  QLabel *m_dashboardFailedRuns = nullptr;
  QLabel *m_dashboardActiveJobs = nullptr;
  QLabel *m_dashboardNextSchedule = nullptr;
  QTableWidget *m_dashboardRecent = nullptr;
  QTableWidget *m_dashboardReview = nullptr;
  QListWidget *m_targetSetList = nullptr;
  QLineEdit *m_targetSetName = nullptr;
  QTextEdit *m_targetSetDescription = nullptr;
  QTableWidget *m_targetTable = nullptr;
  QToolBar *m_toolbar = nullptr;
  QHash<QString, QAction *> m_toolbarActions;
  QMenu *m_moreMenu = nullptr;
  QAction *m_projectWidgetAction = nullptr;
  QAction *m_toolbarSpacerAction = nullptr;
  QAction *m_moreWidgetAction = nullptr;

  void buildUi();
  QWidget *buildDashboardPage();
  QWidget *buildCapturePage();
  QWidget *buildHistoryPage();
  QWidget *buildComparePage();
  QWidget *buildTargetsPage();
  QWidget *buildSchedulesPage();
  QWidget *buildSettingsPage();
  QWidget *buildHelpPage();
  void showAbout();
  void rpcCall(const QString &method, const QJsonObject &params,
               std::function<void(const QJsonObject &)> success = {});
  void refreshAll();
  void refreshProjects();
  void refreshProfiles();
  void refreshDashboard();
  void refreshTargetSets();
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
  void applyReviewFilters();
  void reviewSelected(const QString &status);
  void loadSelectedTargetSet();
  void saveTargetSet();
  void applyToolbarPreferences();
  void openToolbarCustomizer();
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
