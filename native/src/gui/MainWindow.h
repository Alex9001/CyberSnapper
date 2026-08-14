#pragma once

#include "core/Rpc.h"

#include <QJsonArray>
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QTextEdit;
class QTreeWidget;

namespace CyberSnapper {

class MainWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  void connectToAgent();

private:
  RpcClient m_rpc;
  QString m_projectId;
  QJsonArray m_projects;
  QJsonArray m_profiles;

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
  QTreeWidget *m_activeJobs = nullptr;
  QTableWidget *m_history = nullptr;
  QTableWidget *m_artifacts = nullptr;
  QTableWidget *m_comparisons = nullptr;
  QTableWidget *m_schedules = nullptr;
  QCheckBox *m_apiEnabled = nullptr;
  QLabel *m_apiStatus = nullptr;
  QLabel *m_workerStatus = nullptr;
  QSpinBox *m_maximumJobs = nullptr;

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
  void submitCapture();
  QJsonObject captureProfile() const;
  void loadSelectedProfile();
  QString selectedJobId() const;
  QString selectedArtifactId() const;
  QString selectedScheduleId() const;
  void showJobDetails();
  void openSelectedArtifact();
  void setSelectedArtifactAsBaseline();
  void createSchedule();
};

} // namespace CyberSnapper
