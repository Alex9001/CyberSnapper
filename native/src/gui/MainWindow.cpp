#include "gui/MainWindow.h"

#include "core/Models.h"
#include "core/Paths.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QTimeZone>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace CyberSnapper {

namespace {

QWidget *scrollable(QWidget *contents) {
  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(contents);
  return scroll;
}

QString displayTime(const QString &iso) {
  const QDateTime value = QDateTime::fromString(iso, Qt::ISODate);
  return value.isValid() ? value.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : iso;
}

QStringList checkedValues(const QList<QPair<QCheckBox *, QString>> &items) {
  QStringList result;
  for (const auto &[box, value] : items) if (box->isChecked()) result.append(value);
  return result;
}

QJsonArray stringArray(const QStringList &values) {
  QJsonArray out;
  for (const auto &value : values) out.append(value);
  return out;
}

QTableWidgetItem *item(const QString &text, const QString &id = {}) {
  auto *result = new QTableWidgetItem(text);
  if (!id.isEmpty()) result->setData(Qt::UserRole, id);
  return result;
}

QLabel *helperText(const QString &text) {
  auto *label = new QLabel(text);
  label->setObjectName("helperText");
  label->setWordWrap(true);
  return label;
}

void explain(QWidget *widget, const QString &text) {
  widget->setToolTip(text);
  widget->setWhatsThis(text);
  widget->setAccessibleDescription(text);
}

void explainHeader(QTableWidget *table, int column, const QString &text) {
  if (auto *header = table->horizontalHeaderItem(column)) header->setToolTip(text);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_rpc(this) {
  buildUi();
  connect(&m_rpc, &RpcClient::connected, this, [this] {
    m_connectionStatus->setText("Ready");
    m_connectionStatus->setToolTip({});
    m_connectionStatus->setStyleSheet("color: palette(highlight);");
    refreshAll();
  });
  connect(&m_rpc, &RpcClient::disconnected, this, [this] {
    m_connectionStatus->setText("Reconnecting…");
    m_connectionStatus->setStyleSheet("color: palette(mid);");
    QTimer::singleShot(1000, this, &MainWindow::connectToAgent);
  });
  connect(&m_rpc, &RpcClient::connectionError, this, [this](const QString &message) {
    m_connectionStatus->setText("Background service unavailable — retrying…");
    m_connectionStatus->setToolTip(message);
  });
  connect(&m_rpc, &RpcClient::eventReceived, this,
          [this](const QString &event, const QJsonObject &) {
    if (event == "job.event" || event == "queue.changed") refreshJobs();
    if (event == "schedule.changed" || event == "schedule.event") refreshSchedules();
    if (event == "project.changed") { refreshProjects(); refreshProfiles(); refreshJobs(); }
    if (event == "browser.install.finished") refreshSettings();
  });
}

void MainWindow::connectToAgent() { m_rpc.connectToAgent(Paths::agentServerName()); }

void MainWindow::buildUi() {
  setWindowTitle("CyberSnapper");
  setWindowIcon(QIcon(":/cybersnapper/logo.png"));
  resize(1180, 780);
  setMinimumSize(900, 620);

  auto *toolbar = addToolBar("Main navigation");
  toolbar->setObjectName("mainNavigation");
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  toolbar->addWidget(new QLabel("Project: "));
  m_projectCombo = new QComboBox;
  m_projectCombo->setMinimumWidth(190);
  m_projectCombo->setMaximumWidth(260);
  explain(m_projectCombo, "The active project owns capture profiles, history, schedules, baselines, and output files.");
  toolbar->addWidget(m_projectCombo);
  QAction *newProject = toolbar->addAction("New");
  newProject->setShortcut(QKeySequence::New);
  newProject->setToolTip("Create a new portable CyberSnapper project folder");
  newProject->setStatusTip(newProject->toolTip());
  QAction *openProject = toolbar->addAction("Open");
  openProject->setShortcut(QKeySequence::Open);
  openProject->setToolTip("Open an existing CyberSnapper project folder");
  openProject->setStatusTip(openProject->toolTip());
  QAction *refresh = toolbar->addAction("Refresh");
  refresh->setShortcut(QKeySequence::Refresh);
  refresh->setToolTip("Reload projects, jobs, schedules, and runtime status");
  refresh->setStatusTip(refresh->toolTip());
  m_connectionStatus = new QLabel("Starting…");
  statusBar()->addPermanentWidget(m_connectionStatus);

  auto *tabs = new QTabWidget;
  tabs->setObjectName("mainTabs");
  tabs->addTab(buildCapturePage(), "Capture");
  tabs->addTab(buildHistoryPage(), "History");
  tabs->addTab(buildComparePage(), "Compare");
  tabs->addTab(buildSchedulesPage(), "Schedules");
  tabs->addTab(buildSettingsPage(), "Settings");
  tabs->addTab(buildHelpPage(), "Help");
  if (auto *tabBar = tabs->findChild<QTabBar *>()) tabBar->hide();
  setCentralWidget(tabs);

  toolbar->addSeparator();
  auto *navigation = new QActionGroup(this);
  navigation->setExclusive(true);
  QList<QAction *> pageActions;
  const QList<QPair<QString, QString>> pages{
      {"Capture", "Configure and start website captures"},
      {"History", "Review capture jobs and open their artifacts"},
      {"Compare", "Review visual differences and manage baselines"},
      {"Schedules", "Create and manage recurring captures"},
      {"Settings", "Configure browsers, runtime capacity, and the local API"},
      {"Help", "Explain CyberSnapper concepts and controls"},
  };
  for (int index = 0; index < pages.size(); ++index) {
    auto *action = new QAction(pages.at(index).first, navigation);
    action->setCheckable(true);
    action->setToolTip(pages.at(index).second);
    action->setStatusTip(action->toolTip());
    action->setShortcut(index < 5
                            ? QKeySequence(QStringLiteral("Ctrl+%1").arg(index + 1))
                            : QKeySequence::HelpContents);
    toolbar->addAction(action);
    connect(action, &QAction::triggered, tabs, [tabs, index] { tabs->setCurrentIndex(index); });
    pageActions.append(action);
  }
  pageActions.first()->setChecked(true);
  connect(tabs, &QTabWidget::currentChanged, this, [pageActions](int index) {
    if (index >= 0 && index < pageActions.size()) pageActions.at(index)->setChecked(true);
  });
  auto *spacer = new QWidget;
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(spacer);
  QAction *about = toolbar->addAction("About");
  about->setToolTip("Version, architecture, and license information");
  about->setStatusTip(about->toolTip());
  connect(about, &QAction::triggered, this, &MainWindow::showAbout);

  // Keep every primary destination visible instead of letting QToolBar move
  // Help or About into its overflow menu on narrower platforms or font sizes.
  setMinimumWidth(qMax(minimumWidth(), toolbar->sizeHint().width() + 12));

  connect(refresh, &QAction::triggered, this, &MainWindow::refreshAll);
  connect(openProject, &QAction::triggered, this, [this] {
    const QString folder = QFileDialog::getExistingDirectory(this, "Open CyberSnapper Project");
    if (!folder.isEmpty()) rpcCall("project.open", {{"root", folder}}, [this](const QJsonObject &) { refreshAll(); });
  });
  connect(newProject, &QAction::triggered, this, [this] {
    const QString folder = QFileDialog::getExistingDirectory(this, "Choose New Project Folder");
    if (folder.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Project", "Project name:", QLineEdit::Normal,
                                               QFileInfo(folder).fileName(), &ok);
    if (ok) rpcCall("project.create", {{"root", folder}, {"name", name}}, [this](const QJsonObject &) { refreshAll(); });
  });
  connect(m_projectCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0) return;
    const QString id = m_projectCombo->itemData(index).toString();
    if (id.isEmpty() || id == m_projectId) return;
    rpcCall("project.setActive", {{"projectId", id}}, [this, id](const QJsonObject &) {
      m_projectId = id;
      refreshProfiles(); refreshJobs(); refreshSchedules();
    });
  });
}

QWidget *MainWindow::buildCapturePage() {
  auto *page = new QWidget;
  auto *pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(12, 12, 12, 12);
  pageLayout->setSpacing(10);

  auto *verticalSplitter = new QSplitter(Qt::Vertical);
  verticalSplitter->setChildrenCollapsible(false);
  auto *configuration = new QSplitter(Qt::Horizontal);
  configuration->setChildrenCollapsible(false);

  auto *leftColumn = new QWidget;
  auto *leftLayout = new QVBoxLayout(leftColumn);
  leftLayout->setContentsMargins(0, 0, 6, 0);
  leftLayout->setSpacing(10);
  auto *targetGroup = new QGroupBox("Targets");
  auto *targetLayout = new QGridLayout(targetGroup);
  targetLayout->setColumnStretch(1, 1);
  m_profileCombo = new QComboBox;
  explain(m_profileCombo, "A profile is a reusable set of viewport, browser, format, and page-preparation options. Changes below are not saved until you save a profile.");
  auto *profileLabel = new QLabel("Profile");
  profileLabel->setBuddy(m_profileCombo);
  explain(profileLabel, m_profileCombo->toolTip());
  targetLayout->addWidget(profileLabel, 0, 0);
  targetLayout->addWidget(m_profileCombo, 0, 1, 1, 3);
  m_urls = new QTextEdit;
  m_urls->setPlaceholderText("https://example.com\nhttps://example.org/about");
  m_urls->setAcceptRichText(false);
  m_urls->setMinimumHeight(90);
  explain(m_urls, "Enter one public HTTP or HTTPS address per line. Each URL is captured with every enabled viewport, browser, and format.");
  auto *urlsLabel = new QLabel("URLs");
  urlsLabel->setBuddy(m_urls);
  explain(urlsLabel, m_urls->toolTip());
  targetLayout->addWidget(urlsLabel, 1, 0, Qt::AlignTop);
  targetLayout->addWidget(m_urls, 1, 1, 1, 3);
  m_captureMode = new QComboBox;
  m_captureMode->addItem("Full page", "fullPage");
  m_captureMode->addItem("Viewport", "viewport");
  m_captureMode->addItem("Element", "element");
  m_captureMode->setItemData(0, "Automatically scroll, then capture all scrollable content.", Qt::ToolTipRole);
  m_captureMode->setItemData(1, "Capture only the configured viewport rectangle.", Qt::ToolTipRole);
  m_captureMode->setItemData(2, "Capture the first visible element matching the CSS selector.", Qt::ToolTipRole);
  explain(m_captureMode, "Full page captures the document, Viewport captures the visible rectangle, and Element captures one CSS-selected element.");
  m_elementSelector = new QLineEdit;
  m_elementSelector->setPlaceholderText("CSS selector, for example main");
  m_elementSelector->setEnabled(false);
  explain(m_elementSelector, "CSS selector for Element mode, such as main, #content, or .product-card.");
  auto *modeLabel = new QLabel("Mode");
  modeLabel->setBuddy(m_captureMode);
  explain(modeLabel, m_captureMode->toolTip());
  auto *elementLabel = new QLabel("Element");
  elementLabel->setBuddy(m_elementSelector);
  explain(elementLabel, m_elementSelector->toolTip());
  targetLayout->addWidget(modeLabel, 2, 0);
  targetLayout->addWidget(m_captureMode, 2, 1);
  targetLayout->addWidget(elementLabel, 2, 2);
  targetLayout->addWidget(m_elementSelector, 2, 3);
  leftLayout->addWidget(targetGroup, 1);

  auto *viewportGroup = new QGroupBox("Viewports");
  auto *viewportLayout = new QVBoxLayout(viewportGroup);
  m_viewports = new QTableWidget(0, 6);
  m_viewports->setHorizontalHeaderLabels({"Use", "Name", "Width", "Height", "Pixel ratio", "Mobile mode"});
  explain(m_viewports, "Each enabled row creates a browser viewport. Hover a column heading for its exact meaning.");
  explainHeader(m_viewports, 0, "Include this viewport in the capture job.");
  explainHeader(m_viewports, 1, "A descriptive name used in filenames, history, and comparisons.");
  explainHeader(m_viewports, 2, "Viewport width in CSS pixels. This controls responsive page layout.");
  explainHeader(m_viewports, 3, "Viewport height in CSS pixels. This controls the initially visible page area.");
  explainHeader(m_viewports, 4, "Device pixels per CSS pixel. Use 1× for standard output or 2× for high-density/Retina output.");
  explainHeader(m_viewports, 5, "Enable browser mobile viewport behavior and touch input. This does not imitate a specific phone or change the user agent.");
  m_viewports->setMinimumHeight(145);
  m_viewports->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_viewports->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  for (int column : {0, 2, 3, 4, 5}) {
    m_viewports->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  }
  m_viewports->verticalHeader()->setVisible(false);
  viewportLayout->addWidget(m_viewports);
  viewportLayout->addWidget(helperText("Width and height are CSS pixels. Pixel ratio controls output density—at 2×, a viewport capture of a 375 × 812 layout is 750 × 1624 pixels. Mobile mode enables mobile viewport behavior and touch input."));
  auto *viewportActions = new QHBoxLayout;
  auto *addViewport = new QPushButton("Add Viewport…");
  auto *removeViewport = new QPushButton("Remove Selected");
  explain(addViewport, "Add a custom viewport row.");
  explain(removeViewport, "Remove the selected viewport row. At least one row is retained.");
  viewportActions->addWidget(addViewport);
  viewportActions->addWidget(removeViewport);
  viewportActions->addStretch();
  viewportLayout->addLayout(viewportActions);
  leftLayout->addWidget(viewportGroup, 1);

  auto *rightColumn = new QWidget;
  auto *rightLayout = new QVBoxLayout(rightColumn);
  rightLayout->setContentsMargins(6, 0, 0, 0);
  rightLayout->setSpacing(10);
  auto *outputGroup = new QGroupBox("Output");
  auto *outputLayout = new QGridLayout(outputGroup);
  m_chromium = new QCheckBox("Chromium"); m_chromium->setChecked(true);
  m_firefox = new QCheckBox("Firefox");
  m_webkit = new QCheckBox("WebKit");
  m_png = new QCheckBox("PNG"); m_png->setChecked(true);
  m_webp = new QCheckBox("WebP");
  m_avif = new QCheckBox("AVIF");
  m_pdf = new QCheckBox("PDF");
  explain(m_chromium, "Capture using the Chromium browser engine.");
  explain(m_firefox, "Capture using the Firefox browser engine. Install it from Settings first.");
  explain(m_webkit, "Capture using the WebKit browser engine. Install it from Settings first.");
  explain(m_png, "Lossless raster image; best for visual comparisons.");
  explain(m_webp, "Smaller modern raster image with profile-controlled quality.");
  explain(m_avif, "Highly compressed modern raster image with profile-controlled quality.");
  explain(m_pdf, "Printable PDF output. PDF capture is available only with Chromium.");
  outputLayout->addWidget(new QLabel("Browsers"), 0, 0);
  outputLayout->addWidget(m_chromium, 0, 1);
  outputLayout->addWidget(m_firefox, 0, 2);
  outputLayout->addWidget(m_webkit, 0, 3);
  outputLayout->addWidget(new QLabel("Formats"), 1, 0);
  outputLayout->addWidget(m_png, 1, 1);
  outputLayout->addWidget(m_webp, 1, 2);
  outputLayout->addWidget(m_avif, 1, 3);
  outputLayout->addWidget(m_pdf, 1, 4);
  outputLayout->setColumnStretch(5, 1);
  rightLayout->addWidget(outputGroup);

  auto *timingGroup = new QGroupBox("Page preparation");
  auto *timing = new QGridLayout(timingGroup);
  m_initialDelay = new QDoubleSpinBox; m_initialDelay->setRange(0, 300); m_initialDelay->setValue(1.5); m_initialDelay->setSuffix(" s");
  m_scrollDelay = new QDoubleSpinBox; m_scrollDelay->setRange(0, 300); m_scrollDelay->setValue(1.8); m_scrollDelay->setSuffix(" s");
  m_finalDelay = new QDoubleSpinBox; m_finalDelay->setRange(0, 300); m_finalDelay->setValue(1.0); m_finalDelay->setSuffix(" s");
  m_concurrency = new QSpinBox; m_concurrency->setRange(1, 10); m_concurrency->setValue(1);
  m_blockPopups = new QCheckBox("Block common overlays");
  m_waitSelector = new QLineEdit; m_waitSelector->setPlaceholderText("Optional CSS selector");
  m_hideSelectors = new QLineEdit; m_hideSelectors->setPlaceholderText("Comma-separated CSS selectors");
  explain(m_initialDelay, "Seconds to let the page settle immediately after it loads.");
  explain(m_scrollDelay, "Seconds to let lazy-loaded content settle after automatic full-page scrolling.");
  explain(m_finalDelay, "Seconds to wait after elements are hidden and immediately before capture.");
  explain(m_concurrency, "Browser pages processed in parallel inside this job. Higher values use more CPU and memory.");
  explain(m_blockPopups, "Hide common cookie banners, newsletter dialogs, chat widgets, and modal overlays before capture.");
  explain(m_waitSelector, "Optional CSS selector that must become visible before page preparation continues.");
  explain(m_hideSelectors, "Comma-separated CSS selectors to hide before capture, such as .timestamp, .ad, #chat-widget.");
  timing->addWidget(helperText("Order: load → settle → optional full-page scroll → settle → hide elements → settle → capture."), 0, 0, 1, 6);
  timing->addWidget(new QLabel("After load"), 1, 0);
  timing->addWidget(m_initialDelay, 1, 1);
  timing->addWidget(new QLabel("After scroll"), 1, 2);
  timing->addWidget(m_scrollDelay, 1, 3);
  timing->addWidget(new QLabel("Before capture"), 1, 4);
  timing->addWidget(m_finalDelay, 1, 5);
  timing->addWidget(new QLabel("Parallel pages"), 2, 0);
  timing->addWidget(m_concurrency, 2, 1);
  timing->addWidget(m_blockPopups, 2, 2, 1, 4);
  timing->addWidget(new QLabel("Wait for element"), 3, 0);
  timing->addWidget(m_waitSelector, 3, 1, 1, 5);
  timing->addWidget(new QLabel("Hide elements"), 4, 0);
  timing->addWidget(m_hideSelectors, 4, 1, 1, 5);
  for (int column : {1, 3, 5}) timing->setColumnStretch(column, 1);
  rightLayout->addWidget(timingGroup);

  auto *comparisonGroup = new QGroupBox("Visual comparison");
  auto *comparisonLayout = new QGridLayout(comparisonGroup);
  m_comparisonEnabled = new QCheckBox("Compare captures with a saved baseline");
  m_pixelThreshold = new QDoubleSpinBox;
  m_pixelThreshold->setRange(0, 100); m_pixelThreshold->setDecimals(1); m_pixelThreshold->setValue(10); m_pixelThreshold->setSuffix(" %");
  m_mismatchThreshold = new QDoubleSpinBox;
  m_mismatchThreshold->setRange(0, 100); m_mismatchThreshold->setDecimals(3); m_mismatchThreshold->setValue(0.1); m_mismatchThreshold->setSuffix(" %");
  m_comparisonIgnoreSelectors = new QLineEdit;
  m_comparisonIgnoreSelectors->setPlaceholderText("Optional comma-separated CSS selectors");
  explain(m_comparisonEnabled, "Compare future non-PDF captures against a baseline with the same URL, browser, viewport, mode, and format.");
  explain(m_pixelThreshold, "How different a pixel must be before it counts as changed. Higher values ignore smaller color differences.");
  explain(m_mismatchThreshold, "Maximum percentage of changed pixels allowed before the comparison is marked as different.");
  explain(m_comparisonIgnoreSelectors, "When comparison is enabled, these elements are hidden before capture to stabilize dynamic regions such as timestamps or rotating content.");
  comparisonLayout->addWidget(m_comparisonEnabled, 0, 0, 1, 4);
  comparisonLayout->addWidget(new QLabel("Pixel sensitivity"), 1, 0);
  comparisonLayout->addWidget(m_pixelThreshold, 1, 1);
  comparisonLayout->addWidget(new QLabel("Allowed difference"), 1, 2);
  comparisonLayout->addWidget(m_mismatchThreshold, 1, 3);
  comparisonLayout->addWidget(new QLabel("Hide dynamic elements"), 2, 0);
  comparisonLayout->addWidget(m_comparisonIgnoreSelectors, 2, 1, 1, 3);
  comparisonLayout->setColumnStretch(3, 1);
  rightLayout->addWidget(comparisonGroup);

  auto *actions = new QHBoxLayout;
  auto *capture = new QPushButton("Start Capture");
  capture->setDefault(true);
  capture->setObjectName("primaryAction");
  capture->setMinimumHeight(38);
  auto *saveProfile = new QPushButton("Save as Profile…");
  explain(capture, "Queue one capture for every URL × enabled viewport × browser × format combination.");
  explain(saveProfile, "Create a new reusable profile from the current options.");
  actions->addWidget(capture);
  actions->addWidget(saveProfile);
  actions->addStretch();
  rightLayout->addLayout(actions);
  rightLayout->addStretch();

  configuration->addWidget(leftColumn);
  configuration->addWidget(rightColumn);
  configuration->setStretchFactor(0, 6);
  configuration->setStretchFactor(1, 5);
  configuration->setSizes({620, 500});

  auto *jobsGroup = new QGroupBox("Active jobs");
  auto *jobsLayout = new QVBoxLayout(jobsGroup);
  m_activeJobs = new QTreeWidget;
  m_activeJobs->setHeaderLabels({"Job", "Status", "Completed", "Failed", "Started"});
  m_activeJobs->setRootIsDecorated(false);
  m_activeJobs->setSelectionMode(QAbstractItemView::SingleSelection);
  m_activeJobs->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int column = 1; column < 5; ++column) m_activeJobs->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  jobsLayout->addWidget(m_activeJobs);
  auto *jobActions = new QHBoxLayout;
  auto *cancel = new QPushButton("Cancel Selected Job");
  jobActions->addWidget(cancel);
  jobActions->addStretch();
  jobsLayout->addLayout(jobActions);

  verticalSplitter->addWidget(configuration);
  verticalSplitter->addWidget(jobsGroup);
  verticalSplitter->setStretchFactor(0, 4);
  verticalSplitter->setStretchFactor(1, 2);
  verticalSplitter->setSizes({430, 190});
  pageLayout->addWidget(verticalSplitter);

  connect(m_captureMode, &QComboBox::currentIndexChanged, this,
          [this] { m_elementSelector->setEnabled(m_captureMode->currentData().toString() == "element"); });
  const auto updateComparisonControls = [this] {
    const bool enabled = m_comparisonEnabled->isChecked();
    m_pixelThreshold->setEnabled(enabled);
    m_mismatchThreshold->setEnabled(enabled);
    m_comparisonIgnoreSelectors->setEnabled(enabled);
  };
  connect(m_comparisonEnabled, &QCheckBox::toggled, this, updateComparisonControls);
  updateComparisonControls();
  connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::loadSelectedProfile);
  connect(addViewport, &QPushButton::clicked, this, [this] {
    const int row = m_viewports->rowCount();
    m_viewports->insertRow(row);
    auto *enabled = item(QString()); enabled->setCheckState(Qt::Checked); enabled->setToolTip("Include this viewport in captures.");
    auto *name = item("Custom", newId());
    auto *mobile = item(QString()); mobile->setCheckState(Qt::Unchecked); mobile->setToolTip("Enable mobile viewport behavior and touch input.");
    auto *pixelRatio = item("1"); pixelRatio->setToolTip("Device pixels per CSS pixel. Use 2 for high-density output.");
    m_viewports->setItem(row, 0, enabled); m_viewports->setItem(row, 1, name);
    m_viewports->setItem(row, 2, item("1440")); m_viewports->setItem(row, 3, item("900"));
    m_viewports->setItem(row, 4, pixelRatio); m_viewports->setItem(row, 5, mobile);
  });
  connect(removeViewport, &QPushButton::clicked, this, [this] {
    const int row = m_viewports->currentRow();
    if (row >= 0 && m_viewports->rowCount() > 1) m_viewports->removeRow(row);
  });
  connect(capture, &QPushButton::clicked, this, &MainWindow::submitCapture);
  connect(saveProfile, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save Capture Profile", "Profile name:",
                                               QLineEdit::Normal, "My profile", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QJsonObject profile = captureProfile();
    profile.insert("id", newId());
    profile.insert("name", name);
    rpcCall("profile.save", {{"projectId", m_projectId}, {"profile", profile}},
            [this](const QJsonObject &) { refreshProfiles(); statusBar()->showMessage("Profile saved", 3000); });
  });
  connect(cancel, &QPushButton::clicked, this, [this] {
    const auto selected = m_activeJobs->selectedItems();
    if (!selected.isEmpty()) rpcCall("job.cancel", {{"jobId", selected.first()->data(0, Qt::UserRole).toString()}});
  });
  return page;
}

QWidget *MainWindow::buildHistoryPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  auto *splitter = new QSplitter(Qt::Vertical);
  m_history = new QTableWidget(0, 6);
  m_history->setHorizontalHeaderLabels({"Created", "Status", "Source", "Files created", "File errors", "Job ID"});
  m_history->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_history->setSelectionMode(QAbstractItemView::SingleSelection);
  m_history->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_history->horizontalHeader()->setStretchLastSection(true);
  m_artifacts = new QTableWidget(0, 7);
  m_artifacts->setHorizontalHeaderLabels({"Viewport", "Browser", "Format", "Size", "Status", "URL", "File"});
  m_artifacts->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_artifacts->setSelectionMode(QAbstractItemView::SingleSelection);
  m_artifacts->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_artifacts->horizontalHeader()->setStretchLastSection(true);
  auto *jobsGroup = new QGroupBox("Capture jobs");
  auto *jobsLayout = new QVBoxLayout(jobsGroup);
  jobsLayout->addWidget(m_history);
  auto *artifactsGroup = new QGroupBox("Files from the selected job");
  auto *artifactsLayout = new QVBoxLayout(artifactsGroup);
  artifactsLayout->addWidget(m_artifacts);
  splitter->addWidget(jobsGroup);
  splitter->addWidget(artifactsGroup);
  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 1);
  layout->addWidget(splitter);
  auto *buttons = new QHBoxLayout;
  auto *open = new QPushButton("Open Artifact");
  auto *baseline = new QPushButton("Set as Baseline");
  auto *retry = new QPushButton("Retry Job");
  auto *cancel = new QPushButton("Cancel Job");
  auto *refresh = new QPushButton("Refresh");
  baseline->setEnabled(false);
  explain(baseline, "Select a successful PNG, WebP, or AVIF file to use as the reference image for future visual comparisons.");
  for (auto *button : {open, baseline, retry, cancel, refresh}) buttons->addWidget(button);
  buttons->addStretch();
  layout->addLayout(buttons);
  connect(m_history, &QTableWidget::itemSelectionChanged, this, &MainWindow::showJobDetails);
  connect(m_artifacts, &QTableWidget::itemSelectionChanged, this, [this, baseline] {
    const int row = m_artifacts->currentRow();
    const bool eligible = row >= 0 &&
        m_artifacts->item(row, 2)->text().compare("pdf", Qt::CaseInsensitive) != 0 &&
        m_artifacts->item(row, 4)->text() == "succeeded";
    baseline->setEnabled(eligible);
  });
  connect(m_artifacts, &QTableWidget::cellDoubleClicked, this, [this] { openSelectedArtifact(); });
  connect(open, &QPushButton::clicked, this, &MainWindow::openSelectedArtifact);
  connect(baseline, &QPushButton::clicked, this, &MainWindow::setSelectedArtifactAsBaseline);
  connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshJobs);
  connect(retry, &QPushButton::clicked, this, [this] {
    const QString id = selectedJobId();
    if (!id.isEmpty()) rpcCall("job.retry", {{"jobId", id}}, [this](const QJsonObject &) { refreshJobs(); });
  });
  connect(cancel, &QPushButton::clicked, this, [this] {
    const QString id = selectedJobId();
    if (!id.isEmpty()) rpcCall("job.cancel", {{"jobId", id}});
  });
  return page;
}

QWidget *MainWindow::buildComparePage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  auto *intro = new QLabel("This page shows comparisons for the job currently selected in History. To begin, select a non-PDF file in History and choose Set as Baseline. Future captures with visual comparison enabled are matched by URL, browser, viewport, mode, and format.");
  intro->setWordWrap(true);
  layout->addWidget(intro);
  m_comparisons = new QTableWidget(0, 5);
  m_comparisons->setHorizontalHeaderLabels({"Status", "Mismatch", "Viewport / Browser", "Current", "Diff"});
  m_comparisons->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_comparisons->setSelectionBehavior(QAbstractItemView::SelectRows);
  auto *comparisonHeader = m_comparisons->horizontalHeader();
  comparisonHeader->setStretchLastSection(false);
  comparisonHeader->setMinimumSectionSize(90);
  comparisonHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  comparisonHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  comparisonHeader->setSectionResizeMode(2, QHeaderView::Interactive);
  comparisonHeader->setSectionResizeMode(3, QHeaderView::Interactive);
  comparisonHeader->setSectionResizeMode(4, QHeaderView::Stretch);
  m_comparisons->setColumnWidth(2, 240);
  m_comparisons->setColumnWidth(3, 150);
  layout->addWidget(m_comparisons, 1);
  auto *history = new QPushButton("Choose a Baseline in History");
  layout->addWidget(history, 0, Qt::AlignLeft);
  connect(history, &QPushButton::clicked, this, [this] {
    if (auto *tabs = findChild<QTabWidget *>("mainTabs")) tabs->setCurrentIndex(1);
  });
  return page;
}

QWidget *MainWindow::buildSchedulesPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  layout->addWidget(helperText("Schedules run a saved capture profile in the displayed local time zone. Run Now starts the selected schedule immediately without changing its next planned run."));
  m_schedules = new QTableWidget(0, 6);
  m_schedules->setHorizontalHeaderLabels({"Name", "Enabled", "Recurrence", "Next run", "Last status", "ID"});
  m_schedules->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_schedules->setSelectionMode(QAbstractItemView::SingleSelection);
  m_schedules->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_schedules->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(m_schedules);
  auto *buttons = new QHBoxLayout;
  auto *add = new QPushButton("New Schedule");
  auto *run = new QPushButton("Run Now");
  auto *remove = new QPushButton("Remove");
  auto *refresh = new QPushButton("Refresh");
  explain(add, "Create a recurring capture using the profile currently selected on Capture.");
  explain(run, "Run the selected schedule immediately.");
  explain(remove, "Permanently remove the selected schedule after confirmation.");
  for (auto *button : {add, run, remove, refresh}) buttons->addWidget(button);
  buttons->addStretch();
  layout->addLayout(buttons);
  connect(add, &QPushButton::clicked, this, &MainWindow::createSchedule);
  connect(run, &QPushButton::clicked, this, [this] {
    const QString id = selectedScheduleId();
    if (!id.isEmpty()) rpcCall("schedule.runNow", {{"projectId", m_projectId}, {"scheduleId", id}});
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    const QString id = selectedScheduleId();
    if (id.isEmpty()) return;
    if (QMessageBox::question(this, "Remove schedule", "Remove the selected schedule?") == QMessageBox::Yes) {
      rpcCall("schedule.remove", {{"projectId", m_projectId}, {"scheduleId", id}}, [this](const QJsonObject &) { refreshSchedules(); });
    }
  });
  connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshSchedules);
  return page;
}

QWidget *MainWindow::buildSettingsPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  auto *apiGroup = new QGroupBox("Local REST API");
  auto *apiForm = new QFormLayout(apiGroup);
  m_apiEnabled = new QCheckBox("Enable localhost API");
  m_apiStatus = new QLabel("Checking…");
  auto *regenerate = new QPushButton("Generate New Token…");
  explain(m_apiEnabled, "Allow automation tools on this computer to submit and inspect captures through an authenticated API. It never listens on the network.");
  explain(regenerate, "Replace the current bearer token immediately. Existing integrations will stop authenticating.");
  apiForm->addRow(helperText("Optional automation interface for tools on this computer. It binds only to 127.0.0.1 and requires a bearer token."));
  apiForm->addRow(QString(), m_apiEnabled);
  apiForm->addRow("Status", m_apiStatus);
  apiForm->addRow(QString(), regenerate);
  layout->addWidget(apiGroup);
  auto *runtimeGroup = new QGroupBox("Capture runtime");
  auto *runtimeForm = new QFormLayout(runtimeGroup);
  m_workerStatus = new QLabel;
  m_workerStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_maximumJobs = new QSpinBox; m_maximumJobs->setRange(1, 2);
  auto *saveRuntime = new QPushButton("Save Runtime Settings");
  explain(m_maximumJobs, "Separate capture jobs allowed to run simultaneously. Each profile’s Parallel pages setting controls concurrency inside one job.");
  explain(saveRuntime, "Apply the simultaneous-job limit to the background capture service.");
  auto *browserButtons = new QWidget;
  auto *browserButtonsLayout = new QHBoxLayout(browserButtons);
  browserButtonsLayout->setContentsMargins(0, 0, 0, 0);
  auto *installChromium = new QPushButton("Install Chromium");
  auto *installFirefox = new QPushButton("Install Firefox");
  auto *installWebKit = new QPushButton("Install WebKit");
  explain(installChromium, "Install or repair CyberSnapper’s managed Chromium browser engine.");
  explain(installFirefox, "Install or repair CyberSnapper’s managed Firefox browser engine.");
  explain(installWebKit, "Install or repair CyberSnapper’s managed WebKit browser engine.");
  for (auto *button : {installChromium, installFirefox, installWebKit}) browserButtonsLayout->addWidget(button);
  browserButtonsLayout->addStretch();
  runtimeForm->addRow(helperText("The capture engine runs browser automation in the background. Browser installation state is available by hovering over Engine status."));
  runtimeForm->addRow("Engine status", m_workerStatus);
  runtimeForm->addRow("Browser engines", browserButtons);
  runtimeForm->addRow("Simultaneous jobs", m_maximumJobs);
  runtimeForm->addRow(QString(), saveRuntime);
  layout->addWidget(runtimeGroup);
  layout->addStretch();
  connect(m_apiEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
    if (!m_rpc.isConnected()) return;
    rpcCall("api.setEnabled", {{"enabled", enabled}}, [this](const QJsonObject &result) {
      if (result.contains("token")) {
        QMessageBox::information(this, "API token — copy it now",
                                 "This token is only shown once:\n\n" + result.value("token").toString());
      }
      refreshSettings();
    });
  });
  connect(regenerate, &QPushButton::clicked, this, [this] {
    if (QMessageBox::question(this, "Generate new token", "Existing API clients will stop authenticating. Continue?") != QMessageBox::Yes) return;
    rpcCall("api.regenerateToken", {}, [this](const QJsonObject &result) {
      QMessageBox::information(this, "API token — copy it now",
                               "This token is only shown once:\n\n" + result.value("token").toString());
      refreshSettings();
    });
  });
  connect(saveRuntime, &QPushButton::clicked, this, [this] {
    rpcCall("settings.set", {{"maximumActiveJobs", m_maximumJobs->value()}}, [this](const QJsonObject &) {
      statusBar()->showMessage("Runtime settings saved", 3000);
    });
  });
  const auto install = [this](const QString &engine) {
    rpcCall("browser.install", {{"engine", engine}}, [this, engine](const QJsonObject &) {
      statusBar()->showMessage("Installing " + engine + " in the background…", 5000);
    });
  };
  connect(installChromium, &QPushButton::clicked, this, [install] { install("chromium"); });
  connect(installFirefox, &QPushButton::clicked, this, [install] { install("firefox"); });
  connect(installWebKit, &QPushButton::clicked, this, [install] { install("webkit"); });
  return scrollable(page);
}

QWidget *MainWindow::buildHelpPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(18, 14, 18, 14);
  auto *title = new QLabel("CyberSnapper Help");
  title->setObjectName("pageTitle");
  layout->addWidget(title);
  layout->addWidget(helperText("Most controls also have a tooltip—hover over a field or table heading for its exact behavior."));

  auto *help = new QTextBrowser;
  help->setOpenExternalLinks(true);
  help->setHtml(QStringLiteral(R"HTML(
    <h2>Quick start</h2>
    <ol>
      <li>Choose or create a <b>Project</b>. A project keeps profiles, history, schedules, baselines, and files together.</li>
      <li>On <b>Capture</b>, enter one public HTTP or HTTPS URL per line.</li>
      <li>Choose the viewports, browsers, formats, and capture mode you need.</li>
      <li>Select <b>Start Capture</b>. Progress appears under Active jobs and finished files appear in History.</li>
    </ol>

    <h2>Navigation</h2>
    <p>The single header contains project commands and every application section. Use Ctrl+1 through Ctrl+5 for Capture through Settings, or F1 for this Help page.</p>

    <h2>Profiles</h2>
    <p>A profile is a reusable set of capture options. Editing controls changes the next capture only. <b>Save as Profile</b> creates a new reusable profile from the current settings.</p>

    <h2>Viewports</h2>
    <table cellspacing="6">
      <tr><td><b>Use</b></td><td>Includes that row in the capture.</td></tr>
      <tr><td><b>Width / Height</b></td><td>The browser layout size in CSS pixels. These values trigger responsive breakpoints.</td></tr>
      <tr><td><b>Pixel ratio</b></td><td>Device pixels per CSS pixel. At 2×, a viewport capture of a 375 × 812 layout produces a 750 × 1624 image without changing the page layout.</td></tr>
      <tr><td><b>Mobile mode</b></td><td>Enables mobile viewport behavior and touch input. It does not imitate a particular phone or change the browser user agent.</td></tr>
    </table>

    <h2>Capture modes</h2>
    <ul>
      <li><b>Full page</b> automatically scrolls, waits for lazy content, then captures all scrollable content.</li>
      <li><b>Viewport</b> captures only the configured viewport rectangle.</li>
      <li><b>Element</b> captures the first visible element matching the CSS selector.</li>
    </ul>

    <h2>Page preparation</h2>
    <p>The sequence is: load the page, wait After load, optionally auto-scroll, wait After scroll, hide requested elements and common overlays, wait Before capture, then capture. <b>Parallel pages</b> controls browser pages inside one job; Settings → Simultaneous jobs controls separate jobs.</p>

    <h2>Visual comparison</h2>
    <p>Select a non-PDF file in History and choose <b>Set as Baseline</b>. Enable comparison in a capture profile. Future captures are matched by URL, browser, viewport, mode, and format.</p>
    <p><b>Pixel sensitivity</b> decides how large a color change must be before a pixel counts as changed. <b>Allowed difference</b> is the percentage of changed pixels permitted before the result is marked different. Dynamic-element selectors are hidden in comparison-enabled captures to stabilize timestamps, ads, and rotating content.</p>

    <h2>History, schedules, and settings</h2>
    <p><b>History</b> shows jobs above and files from the selected job below. <b>Schedules</b> runs saved profiles at recurring times. <b>Settings</b> installs browser engines, limits simultaneous jobs, and optionally enables the authenticated localhost API.</p>

    <p><a href="https://github.com/Alex9001/CyberSnapper">CyberSnapper on GitHub</a></p>
  )HTML"));
  layout->addWidget(help, 1);
  return page;
}

void MainWindow::showAbout() {
  QMessageBox::about(
      this, "About CyberSnapper",
      QStringLiteral("<h2>CyberSnapper %1</h2>"
                     "<p>Native cross-platform website capture and visual regression.</p>"
                     "<p>Qt %2 interface · background capture agent · private Playwright worker</p>"
                     "<p>Runs on macOS, Windows, and Linux.</p>"
                     "<p><a href=\"https://github.com/Alex9001/CyberSnapper\">github.com/Alex9001/CyberSnapper</a></p>"
                     "<p>ISC License</p>")
          .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::rpcCall(const QString &method, const QJsonObject &params,
                         std::function<void(const QJsonObject &)> success) {
  if (!m_rpc.isConnected()) {
    QMessageBox::warning(this, "Agent unavailable", "CyberSnapper is still connecting to its background agent.");
    return;
  }
  m_rpc.call(method, params, [this, success = std::move(success)](const QJsonObject &result, const QJsonObject &error) {
    if (!error.isEmpty()) {
      QMessageBox::warning(this, "CyberSnapper", error.value("message").toString("Request failed"));
    } else if (success) {
      success(result);
    }
  });
}

void MainWindow::refreshAll() {
  refreshProjects();
  refreshProfiles();
  refreshJobs();
  refreshSchedules();
  refreshSettings();
}

void MainWindow::refreshProjects() {
  rpcCall("project.list", {}, [this](const QJsonObject &result) {
    m_projects = result.value("projects").toArray();
    m_projectId = result.value("activeProjectId").toString();
    m_projectCombo->blockSignals(true);
    m_projectCombo->clear();
    int selected = -1;
    for (const auto &value : m_projects) {
      const auto project = value.toObject();
      m_projectCombo->addItem(project.value("name").toString(), project.value("id").toString());
      const int row = m_projectCombo->count() - 1;
      m_projectCombo->setItemData(row, project.value("root").toString(), Qt::ToolTipRole);
      if (project.value("id").toString() == m_projectId) selected = row;
    }
    m_projectCombo->setCurrentIndex(selected);
    m_projectCombo->blockSignals(false);
    refreshProfiles();
    refreshJobs();
    refreshSchedules();
  });
}

void MainWindow::refreshProfiles() {
  if (m_projectId.isEmpty()) return;
  rpcCall("profile.list", {{"projectId", m_projectId}}, [this](const QJsonObject &result) {
    m_profiles = result.value("profiles").toArray();
    const QString old = m_profileCombo->currentData().toString();
    m_profileCombo->clear();
    for (const auto &value : m_profiles) {
      const auto profile = value.toObject();
      m_profileCombo->addItem(profile.value("name").toString(), profile.value("id").toString());
    }
    const int index = m_profileCombo->findData(old.isEmpty() ? "default" : old);
    if (index >= 0) m_profileCombo->setCurrentIndex(index);
    loadSelectedProfile();
  });
}

void MainWindow::refreshJobs() {
  if (m_projectId.isEmpty()) return;
  rpcCall("job.list", {{"projectId", m_projectId}, {"limit", 500}}, [this](const QJsonObject &result) {
    const QJsonArray jobs = result.value("jobs").toArray();
    m_history->setRowCount(jobs.size());
    m_activeJobs->clear();
    for (int row = 0; row < jobs.size(); ++row) {
      const QJsonObject job = jobs.at(row).toObject();
      const QString id = job.value("id").toString();
      m_history->setItem(row, 0, item(displayTime(job.value("createdAt").toString()), id));
      m_history->setItem(row, 1, item(job.value("status").toString()));
      m_history->setItem(row, 2, item(job.value("source").toString()));
      m_history->setItem(row, 3, item(QString::number(job.value("completedArtifacts").toInt())));
      m_history->setItem(row, 4, item(QString::number(job.value("failedArtifacts").toInt())));
      m_history->setItem(row, 5, item(id));
      if (!QStringList{"succeeded", "partial", "failed", "cancelled", "interrupted"}.contains(job.value("status").toString())) {
        auto *active = new QTreeWidgetItem({id.left(8), job.value("status").toString(),
                                            QString::number(job.value("completedArtifacts").toInt()),
                                            QString::number(job.value("failedArtifacts").toInt()),
                                            displayTime(job.value("startedAt").toString())});
        active->setData(0, Qt::UserRole, id);
        m_activeJobs->addTopLevelItem(active);
      }
    }
  });
}

void MainWindow::refreshSchedules() {
  if (m_projectId.isEmpty()) return;
  rpcCall("schedule.list", {{"projectId", m_projectId}}, [this](const QJsonObject &result) {
    const auto schedules = result.value("schedules").toArray();
    m_schedules->setRowCount(schedules.size());
    for (int row = 0; row < schedules.size(); ++row) {
      const auto schedule = schedules.at(row).toObject();
      const auto recurrence = schedule.value("recurrence").toObject();
      const QString id = schedule.value("id").toString();
      m_schedules->setItem(row, 0, item(schedule.value("name").toString(), id));
      m_schedules->setItem(row, 1, item(schedule.value("enabled").toBool() ? "Yes" : "No"));
      m_schedules->setItem(row, 2, item(recurrence.value("type").toString()));
      m_schedules->setItem(row, 3, item(displayTime(schedule.value("nextRun").toString())));
      m_schedules->setItem(row, 4, item(schedule.value("lastStatus").toString()));
      m_schedules->setItem(row, 5, item(id));
    }
  });
}

void MainWindow::refreshSettings() {
  rpcCall("settings.get", {}, [this](const QJsonObject &result) {
    const QString workerEntry = result.value("workerEntry").toString();
    m_workerStatus->setProperty("workerEntry", workerEntry);
    m_workerStatus->setText(workerEntry.isEmpty()
                                ? "Not ready — build the capture worker"
                                : "Ready");
    m_workerStatus->setToolTip(workerEntry.isEmpty()
                                   ? "Run npm run build:worker from a source checkout."
                                   : "Worker: " + workerEntry);
    m_maximumJobs->setValue(result.value("maximumActiveJobs").toInt(1));
  });
  rpcCall("api.status", {}, [this](const QJsonObject &result) {
    m_apiEnabled->blockSignals(true);
    m_apiEnabled->setChecked(result.value("enabled").toBool());
    m_apiEnabled->blockSignals(false);
    m_apiStatus->setText(result.value("enabled").toBool()
                             ? QStringLiteral("Listening on http://127.0.0.1:%1/api/v1").arg(result.value("port").toInt())
                             : "Disabled");
  });
  rpcCall("browser.status", {}, [this](const QJsonObject &result) {
    QStringList states;
    const QJsonObject browsers = result.value("browsers").toObject();
    for (const auto &engine : {QString("chromium"), QString("firefox"), QString("webkit")}) {
      states.append(engine + ": " + (browsers.value(engine).toObject().value("installed").toBool() ? "installed" : "not installed"));
    }
    const QString workerEntry = m_workerStatus->property("workerEntry").toString();
    const QString worker = workerEntry.isEmpty() ? QString() : "Worker: " + workerEntry + "\n\n";
    m_workerStatus->setToolTip(worker + "Browser engines:\n" + states.join("\n"));
  });
}

QJsonObject MainWindow::captureProfile() const {
  QJsonObject profile;
  for (const auto &value : m_profiles) {
    if (value.toObject().value("id").toString() == m_profileCombo->currentData().toString()) profile = value.toObject();
  }
  if (profile.isEmpty()) profile = toJson(defaultProfile());
  profile.insert("captureMode", m_captureMode->currentData().toString());
  profile.insert("elementSelector", m_elementSelector->text().trimmed());
  profile.insert("engines", stringArray(checkedValues({{m_chromium, "chromium"}, {m_firefox, "firefox"}, {m_webkit, "webkit"}})));
  profile.insert("formats", stringArray(checkedValues({{m_png, "png"}, {m_webp, "webp"}, {m_avif, "avif"}, {m_pdf, "pdf"}})));
  profile.insert("initialDelay", m_initialDelay->value());
  profile.insert("scrollDelay", m_scrollDelay->value());
  profile.insert("finalDelay", m_finalDelay->value());
  profile.insert("concurrency", m_concurrency->value());
  profile.insert("blockPopups", m_blockPopups->isChecked());
  profile.insert("waitForSelector", m_waitSelector->text().trimmed());
  QStringList hidden;
  for (const auto &part : m_hideSelectors->text().split(',')) if (!part.trimmed().isEmpty()) hidden.append(part.trimmed());
  profile.insert("hideSelectors", stringArray(hidden));
  profile.insert("comparisonEnabled", m_comparisonEnabled->isChecked());
  profile.insert("pixelThreshold", m_pixelThreshold->value() / 100.0);
  profile.insert("mismatchThreshold", m_mismatchThreshold->value() / 100.0);
  QStringList comparisonIgnored;
  for (const auto &part : m_comparisonIgnoreSelectors->text().split(',')) {
    if (!part.trimmed().isEmpty()) comparisonIgnored.append(part.trimmed());
  }
  profile.insert("comparisonIgnoreSelectors", stringArray(comparisonIgnored));
  QJsonArray viewports;
  for (int row = 0; row < m_viewports->rowCount(); ++row) {
    const QString id = m_viewports->item(row, 1)->data(Qt::UserRole).toString();
    viewports.append(QJsonObject{{"id", id.isEmpty() ? newId() : id},
                                 {"name", m_viewports->item(row, 1)->text()},
                                 {"width", m_viewports->item(row, 2)->text().toInt()},
                                 {"height", m_viewports->item(row, 3)->text().toInt()},
                                 {"deviceScaleFactor", m_viewports->item(row, 4)->text().toDouble()},
                                 {"mobile", m_viewports->item(row, 5)->checkState() == Qt::Checked},
                                 {"enabled", m_viewports->item(row, 0)->checkState() == Qt::Checked}});
  }
  profile.insert("viewports", viewports);
  return profile;
}

void MainWindow::loadSelectedProfile() {
  QJsonObject profile;
  for (const auto &value : m_profiles) {
    if (value.toObject().value("id").toString() == m_profileCombo->currentData().toString()) profile = value.toObject();
  }
  if (profile.isEmpty() || !m_viewports) return;
  auto checkValues = [&profile](const char *key, const QList<QPair<QCheckBox *, QString>> &items) {
    QStringList selected;
    for (const auto &value : profile.value(key).toArray()) selected.append(value.toString());
    for (const auto &[box, value] : items) box->setChecked(selected.contains(value));
  };
  checkValues("engines", {{m_chromium, "chromium"}, {m_firefox, "firefox"}, {m_webkit, "webkit"}});
  checkValues("formats", {{m_png, "png"}, {m_webp, "webp"}, {m_avif, "avif"}, {m_pdf, "pdf"}});
  const int modeIndex = m_captureMode->findData(profile.value("captureMode").toString("fullPage"));
  if (modeIndex >= 0) m_captureMode->setCurrentIndex(modeIndex);
  m_elementSelector->setText(profile.value("elementSelector").toString());
  m_initialDelay->setValue(profile.value("initialDelay").toDouble(1.5));
  m_scrollDelay->setValue(profile.value("scrollDelay").toDouble(1.8));
  m_finalDelay->setValue(profile.value("finalDelay").toDouble(1.0));
  m_concurrency->setValue(profile.value("concurrency").toInt(1));
  m_blockPopups->setChecked(profile.value("blockPopups").toBool());
  m_waitSelector->setText(profile.value("waitForSelector").toString());
  QStringList hidden;
  for (const auto &value : profile.value("hideSelectors").toArray()) hidden.append(value.toString());
  m_hideSelectors->setText(hidden.join(", "));
  m_comparisonEnabled->setChecked(profile.value("comparisonEnabled").toBool(false));
  m_pixelThreshold->setValue(profile.value("pixelThreshold").toDouble(0.10) * 100.0);
  m_mismatchThreshold->setValue(profile.value("mismatchThreshold").toDouble(0.001) * 100.0);
  QStringList comparisonIgnored;
  for (const auto &value : profile.value("comparisonIgnoreSelectors").toArray()) comparisonIgnored.append(value.toString());
  m_comparisonIgnoreSelectors->setText(comparisonIgnored.join(", "));
  const QJsonArray viewports = profile.value("viewports").toArray();
  m_viewports->setRowCount(viewports.size());
  for (int row = 0; row < viewports.size(); ++row) {
    const QJsonObject viewport = viewports.at(row).toObject();
    auto *enabled = item(QString()); enabled->setCheckState(viewport.value("enabled").toBool(true) ? Qt::Checked : Qt::Unchecked);
    enabled->setToolTip("Include this viewport in captures.");
    auto *name = item(viewport.value("name").toString(), viewport.value("id").toString());
    auto *mobile = item(QString()); mobile->setCheckState(viewport.value("mobile").toBool() ? Qt::Checked : Qt::Unchecked);
    mobile->setToolTip("Enable mobile viewport behavior and touch input.");
    auto *pixelRatio = item(QString::number(viewport.value("deviceScaleFactor").toDouble(1.0)));
    pixelRatio->setToolTip("Device pixels per CSS pixel. Use 2 for high-density output.");
    m_viewports->setItem(row, 0, enabled); m_viewports->setItem(row, 1, name);
    m_viewports->setItem(row, 2, item(QString::number(viewport.value("width").toInt())));
    m_viewports->setItem(row, 3, item(QString::number(viewport.value("height").toInt())));
    m_viewports->setItem(row, 4, pixelRatio);
    m_viewports->setItem(row, 5, mobile);
  }
}

void MainWindow::submitCapture() {
  QStringList urls;
  for (const auto &line : m_urls->toPlainText().split('\n')) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty()) urls.append(trimmed);
  }
  if (urls.isEmpty()) {
    QMessageBox::information(this, "URLs required", "Enter at least one URL.");
    return;
  }
  if (m_captureMode->currentData().toString() == "element" && m_elementSelector->text().trimmed().isEmpty()) {
    QMessageBox::information(this, "Selector required", "Element capture requires a CSS selector.");
    return;
  }
  rpcCall("job.submit", {{"projectId", m_projectId}, {"profileId", m_profileCombo->currentData().toString()},
                          {"urls", stringArray(urls)}, {"profile", captureProfile()}, {"source", "gui"}},
          [this](const QJsonObject &result) {
    statusBar()->showMessage("Capture queued: " + result.value("jobId").toString().left(8), 5000);
    refreshJobs();
  });
}

QString MainWindow::selectedJobId() const {
  const auto selected = m_history->selectedItems();
  return selected.isEmpty() ? QString{} : m_history->item(selected.first()->row(), 0)->data(Qt::UserRole).toString();
}

QString MainWindow::selectedArtifactId() const {
  const auto selected = m_artifacts->selectedItems();
  return selected.isEmpty() ? QString{} : m_artifacts->item(selected.first()->row(), 0)->data(Qt::UserRole).toString();
}

QString MainWindow::selectedScheduleId() const {
  const auto selected = m_schedules->selectedItems();
  return selected.isEmpty() ? QString{} : m_schedules->item(selected.first()->row(), 0)->data(Qt::UserRole).toString();
}

void MainWindow::showJobDetails() {
  const QString id = selectedJobId();
  if (id.isEmpty()) return;
  rpcCall("job.get", {{"jobId", id}}, [this](const QJsonObject &result) {
    const QJsonObject job = result.value("job").toObject();
    const QJsonArray artifacts = job.value("artifacts").toArray();
    m_artifacts->setRowCount(artifacts.size());
    for (int row = 0; row < artifacts.size(); ++row) {
      const auto artifact = artifacts.at(row).toObject();
      const QString id = artifact.value("id").toString();
      m_artifacts->setItem(row, 0, item(artifact.value("viewportName").toString(), id));
      m_artifacts->setItem(row, 1, item(artifact.value("engine").toString()));
      m_artifacts->setItem(row, 2, item(artifact.value("format").toString()));
      m_artifacts->setItem(row, 3, item(QStringLiteral("%1×%2").arg(artifact.value("width").toInt()).arg(artifact.value("height").toInt())));
      m_artifacts->setItem(row, 4, item(artifact.value("status").toString()));
      m_artifacts->setItem(row, 5, item(artifact.value("url").toString()));
      m_artifacts->setItem(row, 6, item(artifact.value("relativePath").toString()));
    }
    const QJsonArray comparisons = result.value("comparisons").toArray();
    m_comparisons->setRowCount(comparisons.size());
    for (int row = 0; row < comparisons.size(); ++row) {
      const auto comparison = comparisons.at(row).toObject();
      m_comparisons->setItem(row, 0, item(comparison.value("status").toString()));
      m_comparisons->setItem(row, 1, item(QString::number(comparison.value("mismatchRatio").toDouble() * 100.0, 'f', 3) + "%"));
      const QString key = comparison.value("comparisonKey").toString();
      const QStringList keyParts = key.split('|');
      const QString target = keyParts.size() >= 3
          ? keyParts.at(2) + "  /  " + keyParts.at(1)
          : key;
      auto *targetItem = item(target);
      targetItem->setToolTip(key);
      m_comparisons->setItem(row, 2, targetItem);
      const QString artifactId = comparison.value("currentArtifactId").toString();
      auto *artifactItem = item(artifactId.left(12));
      artifactItem->setToolTip(artifactId);
      m_comparisons->setItem(row, 3, artifactItem);
      const QString diffPath = comparison.value("diffRelativePath").toString();
      auto *diffItem = item(QFileInfo(diffPath).fileName());
      diffItem->setToolTip(diffPath);
      m_comparisons->setItem(row, 4, diffItem);
    }
  });
}

void MainWindow::openSelectedArtifact() {
  const QString id = selectedArtifactId();
  if (id.isEmpty()) return;
  rpcCall("artifact.resolve", {{"artifactId", id}}, [this](const QJsonObject &result) {
    const QString path = result.value("absolutePath").toString();
    if (!QFileInfo::exists(path)) {
      QMessageBox::warning(this, "Missing artifact", "The artifact file no longer exists.");
      return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  });
}

void MainWindow::setSelectedArtifactAsBaseline() {
  const QString artifactId = selectedArtifactId();
  if (artifactId.isEmpty()) {
    QMessageBox::information(this, "Choose a file", "Select a non-PDF file from the selected job first.");
    return;
  }
  const int row = m_artifacts->currentRow();
  if (row < 0 || m_artifacts->item(row, 2)->text().compare("pdf", Qt::CaseInsensitive) == 0) {
    QMessageBox::information(this, "Image required", "PDF files cannot be visual-comparison baselines. Select a successful PNG, WebP, or AVIF file.");
    return;
  }
  if (m_artifacts->item(row, 4)->text() != "succeeded") {
    QMessageBox::information(this, "Completed file required", "Only successfully created image files can be used as visual-comparison baselines.");
    return;
  }
  rpcCall("baseline.set", {{"artifactId", artifactId}}, [this](const QJsonObject &) {
    statusBar()->showMessage("Visual comparison baseline updated", 4000);
  });
}

void MainWindow::createSchedule() {
  QDialog dialog(this);
  dialog.setWindowTitle("New Schedule");
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  auto *name = new QLineEdit("Daily capture");
  auto *urls = new QTextEdit(m_urls->toPlainText()); urls->setMaximumHeight(100);
  auto *type = new QComboBox; type->addItems({"Daily", "Weekly", "Monthly", "Interval"});
  auto *time = new QLineEdit("09:00");
  auto *interval = new QSpinBox; interval->setRange(15, 525600); interval->setValue(60); interval->setSuffix(" min");
  form->addRow("Name", name); form->addRow("URLs", urls); form->addRow("Recurrence", type);
  form->addRow("Local time", time); form->addRow("Interval", interval);
  layout->addLayout(form);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  QStringList urlList;
  for (const auto &line : urls->toPlainText().split('\n')) if (!line.trimmed().isEmpty()) urlList.append(line.trimmed());
  QJsonObject recurrence;
  if (type->currentIndex() == 3) recurrence = {{"type", "interval"}, {"minutes", interval->value()}};
  else if (type->currentIndex() == 0) recurrence = {{"type", "daily"}, {"time", time->text()}};
  else if (type->currentIndex() == 1) recurrence = {{"type", "weekly"}, {"time", time->text()}, {"daysOfWeek", QJsonArray{1}}};
  else recurrence = {{"type", "monthly"}, {"time", time->text()}, {"day", 1}};
  recurrence.insert("timeZone", QString::fromUtf8(QTimeZone::systemTimeZoneId()));
  const QJsonObject schedule{{"name", name->text()}, {"enabled", true},
                             {"profileId", m_profileCombo->currentData().toString()},
                             {"urls", stringArray(urlList)}, {"recurrence", recurrence}};
  rpcCall("schedule.upsert", {{"projectId", m_projectId}, {"schedule", schedule}},
          [this](const QJsonObject &) { refreshSchedules(); });
}

} // namespace CyberSnapper
