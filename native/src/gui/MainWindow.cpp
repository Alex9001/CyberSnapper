#include "gui/MainWindow.h"

#include "core/Models.h"
#include "core/Paths.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDateTimeEdit>
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
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimeEdit>
#include <QTimer>
#include <QTimeZone>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

namespace CyberSnapper {

class ImageCanvas final : public QWidget {
public:
  explicit ImageCanvas(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumSize(220, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  void setImage(const QString &path) { m_path = path; m_image.load(path); update(); }
  void clearImage() { m_path.clear(); m_image = {}; update(); }
  bool hasImage() const { return !m_image.isNull(); }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().brush(QPalette::Base));
    if (m_image.isNull()) {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                       m_path.isEmpty() ? "Select a comparison" : "Image unavailable\n" + m_path);
      return;
    }
    const QSize fitted = m_image.size().scaled(size() - QSize(16, 16), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    painter.drawPixmap(target, m_image);
  }

private:
  QString m_path;
  QPixmap m_image;
};

class OverlayCanvas final : public QWidget {
public:
  explicit OverlayCanvas(QWidget *parent = nullptr) : QWidget(parent) { setMinimumSize(460, 300); }
  void setImages(const QString &baseline, const QString &current) {
    m_baseline.load(baseline); m_current.load(current); update();
  }
  void setOpacity(int percent) { m_opacity = qBound(0, percent, 100) / 100.0; update(); }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().brush(QPalette::Base));
    if (m_baseline.isNull() || m_current.isNull()) {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(rect(), Qt::AlignCenter, "Select a comparison");
      return;
    }
    const QSize source(qMax(m_baseline.width(), m_current.width()),
                       qMax(m_baseline.height(), m_current.height()));
    const QSize fitted = source.scaled(size() - QSize(16, 16), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    painter.drawPixmap(target, m_baseline);
    painter.setOpacity(m_opacity);
    painter.drawPixmap(target, m_current);
  }

private:
  QPixmap m_baseline;
  QPixmap m_current;
  qreal m_opacity = 0.5;
};

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
  restoreUiState();
  connect(&m_rpc, &RpcClient::connected, this, [this] {
    m_connectionStatus->setText("Ready");
    m_connectionStatus->setToolTip({});
    m_connectionStatus->setStyleSheet("color: palette(highlight);");
    refreshAll();
    QTimer::singleShot(300, this, &MainWindow::showFirstRun);
  });
  connect(&m_rpc, &RpcClient::disconnected, this, [this] {
    m_connectionStatus->setText("Reconnecting…");
    m_connectionStatus->setStyleSheet("color: palette(mid);");
    if (!m_reconnectPending) {
      m_reconnectPending = true;
      QTimer::singleShot(1000, this, [this] { m_reconnectPending = false; connectToAgent(); });
    }
  });
  connect(&m_rpc, &RpcClient::connectionError, this, [this](const QString &message) {
    m_connectionStatus->setText("Background service unavailable — retrying…");
    m_connectionStatus->setToolTip(message);
    if (!m_reconnectPending) {
      m_reconnectPending = true;
      QTimer::singleShot(1500, this, [this] { m_reconnectPending = false; connectToAgent(); });
    }
  });
  connect(&m_rpc, &RpcClient::eventReceived, this,
          [this](const QString &event, const QJsonObject &) {
    if (event == "job.event" || event == "queue.changed" || event == "schedule.changed" ||
        event == "schedule.event") scheduleRefresh();
    if (event == "project.changed") refreshProjects();
    if (event == "project.settings.changed") refreshSettings();
    if (event == "browser.install.finished") refreshSettings();
  });
}

MainWindow::~MainWindow() { saveUiState(); }

void MainWindow::connectToAgent() {
  if (!m_rpc.isConnected()) m_rpc.connectToAgent(Paths::agentServerName());
}

bool MainWindow::prepareScreenshotScene(const QString &requestedScene) {
  const QString scene = requestedScene.trimmed().toLower();
  const QHash<QString, int> scenes{{"capture", 0}, {"history", 1}, {"compare", 2},
                                  {"schedules", 3}, {"settings", 4}, {"help", 5}};
  if (!scenes.contains(scene) || !m_tabs) return false;
  m_tabs->setCurrentIndex(scenes.value(scene));
  if (scene == "capture" && m_urls && m_urls->toPlainText().trimmed().isEmpty()) {
    m_urls->setPlainText("https://example.com\nhttps://example.org/pricing");
  }
  if (!m_connectionStatus || m_connectionStatus->text() != "Ready" || m_projectId.isEmpty()) return false;
  if (scene == "capture") {
    return m_profileCombo && m_profileCombo->count() > 0 && m_viewports && m_viewports->rowCount() >= 3 &&
           m_startCapture && m_startCapture->isEnabled();
  }
  if (scene == "history") {
    if (!m_history || m_history->rowCount() == 0) return false;
    if (m_history->currentRow() < 0) { m_history->selectRow(0); return false; }
    if (!m_artifacts || m_artifacts->rowCount() == 0) return false;
    if (m_artifacts->currentRow() < 0) m_artifacts->selectRow(0);
    return true;
  }
  if (scene == "compare") {
    if (!m_comparisons || m_comparisons->rowCount() == 0) return false;
    if (m_comparisons->currentRow() < 0) { m_comparisons->selectRow(0); return false; }
    return m_baselineImage && m_baselineImage->hasImage() && m_currentImage && m_currentImage->hasImage() &&
           m_diffImage && m_diffImage->hasImage();
  }
  if (scene == "schedules") {
    if (!m_schedules || m_schedules->rowCount() == 0) return false;
    if (m_schedules->currentRow() < 0) m_schedules->selectRow(0);
    return true;
  }
  return true;
}

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

  m_tabs = new QTabWidget;
  m_tabs->setObjectName("mainTabs");
  m_tabs->addTab(buildCapturePage(), "Capture");
  m_tabs->addTab(buildHistoryPage(), "History");
  m_tabs->addTab(buildComparePage(), "Compare");
  m_tabs->addTab(buildSchedulesPage(), "Schedules");
  m_tabs->addTab(buildSettingsPage(), "Settings");
  m_tabs->addTab(buildHelpPage(), "Help");
  if (auto *tabBar = m_tabs->findChild<QTabBar *>()) tabBar->hide();
  setCentralWidget(m_tabs);

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
    connect(action, &QAction::triggered, m_tabs, [this, index] { m_tabs->setCurrentIndex(index); });
    pageActions.append(action);
  }
  pageActions.first()->setChecked(true);
  connect(m_tabs, &QTabWidget::currentChanged, this, [pageActions](int index) {
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
    if (m_profileDirty && QMessageBox::question(this, "Discard profile changes?",
        "The current profile has unsaved changes. Discard them and switch projects?",
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) {
      m_projectCombo->blockSignals(true);
      m_projectCombo->setCurrentIndex(m_projectCombo->findData(m_projectId));
      m_projectCombo->blockSignals(false);
      return;
    }
    m_profileDirty = false;
    m_loadedProfileId.clear();
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

  m_captureVertical = new QSplitter(Qt::Vertical);
  m_captureVertical->setObjectName("captureVertical");
  m_captureVertical->setChildrenCollapsible(false);
  m_captureColumns = new QSplitter(Qt::Horizontal);
  m_captureColumns->setObjectName("captureColumns");
  m_captureColumns->setChildrenCollapsible(false);

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
  targetLayout->addWidget(m_profileCombo, 0, 1);
  auto *manageProfiles = new QPushButton("Manage…");
  explain(manageProfiles, "Create, rename, duplicate, delete, and edit every profile option in a tabbed editor.");
  targetLayout->addWidget(manageProfiles, 0, 2);
  m_profileState = new QLabel("Saved");
  m_profileState->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  targetLayout->addWidget(m_profileState, 0, 3);
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

  m_capturePlan = helperText("Choose targets and options to see the capture plan.");
  rightLayout->addWidget(m_capturePlan);

  auto *actions = new QHBoxLayout;
  m_startCapture = new QPushButton("Start Capture");
  m_startCapture->setDefault(true);
  m_startCapture->setObjectName("primaryAction");
  m_startCapture->setMinimumHeight(38);
  m_saveProfile = new QPushButton("Save Profile");
  m_revertProfile = new QPushButton("Revert");
  auto *saveAsProfile = new QPushButton("Save As…");
  explain(m_startCapture, "Queue one capture for every URL × enabled viewport × browser × format combination.");
  explain(m_saveProfile, "Save the current options back to the selected profile.");
  explain(m_revertProfile, "Discard unsaved changes and reload the selected profile.");
  explain(saveAsProfile, "Create a new reusable profile from the current options.");
  actions->addWidget(m_startCapture);
  actions->addWidget(m_saveProfile);
  actions->addWidget(m_revertProfile);
  actions->addWidget(saveAsProfile);
  actions->addStretch();
  rightLayout->addLayout(actions);
  rightLayout->addStretch();

  m_captureColumns->addWidget(leftColumn);
  m_captureColumns->addWidget(rightColumn);
  m_captureColumns->setStretchFactor(0, 6);
  m_captureColumns->setStretchFactor(1, 5);
  m_captureColumns->setSizes({620, 500});

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

  m_captureVertical->addWidget(m_captureColumns);
  m_captureVertical->addWidget(jobsGroup);
  m_captureVertical->setStretchFactor(0, 4);
  m_captureVertical->setStretchFactor(1, 2);
  m_captureVertical->setSizes({430, 190});
  pageLayout->addWidget(m_captureVertical);

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
  connect(manageProfiles, &QPushButton::clicked, this, &MainWindow::openProfileManager);
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
  connect(m_startCapture, &QPushButton::clicked, this, &MainWindow::submitCapture);
  connect(m_saveProfile, &QPushButton::clicked, this, &MainWindow::saveCurrentProfile);
  connect(m_revertProfile, &QPushButton::clicked, this, &MainWindow::loadSelectedProfile);
  connect(saveAsProfile, &QPushButton::clicked, this, [this] {
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
  const auto changed = [this] { markProfileDirty(); updateCapturePlan(); };
  for (auto *box : {m_chromium, m_firefox, m_webkit, m_png, m_webp, m_avif, m_pdf,
                    m_blockPopups, m_comparisonEnabled}) {
    connect(box, &QCheckBox::toggled, this, changed);
  }
  for (auto *edit : {m_elementSelector, m_waitSelector, m_hideSelectors, m_comparisonIgnoreSelectors}) {
    connect(edit, &QLineEdit::textChanged, this, changed);
  }
  for (auto *spin : {m_initialDelay, m_scrollDelay, m_finalDelay, m_pixelThreshold, m_mismatchThreshold}) {
    connect(spin, &QDoubleSpinBox::valueChanged, this, changed);
  }
  connect(m_concurrency, &QSpinBox::valueChanged, this, changed);
  connect(m_captureMode, &QComboBox::currentIndexChanged, this, changed);
  connect(m_viewports, &QTableWidget::itemChanged, this, changed);
  connect(m_urls, &QTextEdit::textChanged, this, &MainWindow::updateCapturePlan);
  updateCapturePlan();
  return page;
}

QWidget *MainWindow::buildHistoryPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  auto *filters = new QHBoxLayout;
  m_historySearch = new QLineEdit;
  m_historySearch->setPlaceholderText("Search job ID, URL, or error…");
  m_historyStatus = new QComboBox;
  m_historyStatus->addItems({"All statuses", "queued", "preparing", "running", "succeeded", "partial", "failed", "cancelled", "interrupted"});
  m_historySource = new QComboBox;
  m_historySource->addItems({"All sources", "gui", "api", "retry", "schedule"});
  filters->addWidget(new QLabel("Filter"));
  filters->addWidget(m_historySearch, 1);
  filters->addWidget(m_historyStatus);
  filters->addWidget(m_historySource);
  layout->addLayout(filters);
  m_historySplit = new QSplitter(Qt::Vertical);
  m_historySplit->setObjectName("historySplit");
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
  m_jobSummary = helperText("Select a capture job to see its files and details.");
  jobsLayout->addWidget(m_jobSummary);
  auto *artifactsGroup = new QGroupBox("Files from the selected job");
  auto *artifactsLayout = new QVBoxLayout(artifactsGroup);
  artifactsLayout->addWidget(m_artifacts);
  m_historySplit->addWidget(jobsGroup);
  m_historySplit->addWidget(artifactsGroup);
  m_historySplit->setStretchFactor(0, 2);
  m_historySplit->setStretchFactor(1, 1);
  layout->addWidget(m_historySplit);
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
  connect(m_historySearch, &QLineEdit::textChanged, this, &MainWindow::applyHistoryFilters);
  connect(m_historyStatus, &QComboBox::currentIndexChanged, this, &MainWindow::applyHistoryFilters);
  connect(m_historySource, &QComboBox::currentIndexChanged, this, &MainWindow::applyHistoryFilters);
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
  auto *intro = new QLabel("Review every visual comparison in this project. Select a row to inspect the baseline and current capture side by side or as an adjustable overlay.");
  intro->setWordWrap(true);
  layout->addWidget(intro);
  m_compareSplit = new QSplitter(Qt::Horizontal);
  m_compareSplit->setObjectName("compareSplit");
  m_compareSplit->setChildrenCollapsible(false);
  auto *listPanel = new QWidget;
  auto *listLayout = new QVBoxLayout(listPanel);
  listLayout->setContentsMargins(0, 0, 6, 0);
  m_comparisons = new QTableWidget(0, 5);
  m_comparisons->setHorizontalHeaderLabels({"Created", "Status", "Mismatch", "Viewport / Browser", "URL"});
  m_comparisons->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_comparisons->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_comparisons->setSelectionMode(QAbstractItemView::SingleSelection);
  auto *comparisonHeader = m_comparisons->horizontalHeader();
  comparisonHeader->setStretchLastSection(true);
  comparisonHeader->setMinimumSectionSize(90);
  comparisonHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  comparisonHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  comparisonHeader->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  comparisonHeader->setSectionResizeMode(3, QHeaderView::Interactive);
  comparisonHeader->setSectionResizeMode(4, QHeaderView::Stretch);
  m_comparisons->setColumnWidth(3, 190);
  listPanel->setMinimumWidth(430);
  listLayout->addWidget(m_comparisons, 1);
  auto *listButtons = new QHBoxLayout;
  auto *refresh = new QPushButton("Refresh");
  auto *history = new QPushButton("Set Baseline in History…");
  listButtons->addWidget(refresh);
  listButtons->addWidget(history);
  listButtons->addStretch();
  listLayout->addLayout(listButtons);

  auto *viewerTabs = new QTabWidget;
  auto *sideBySide = new QWidget;
  auto *sideLayout = new QHBoxLayout(sideBySide);
  auto *baselineGroup = new QGroupBox("Baseline");
  auto *baselineLayout = new QVBoxLayout(baselineGroup);
  m_baselineImage = new ImageCanvas;
  baselineLayout->addWidget(m_baselineImage);
  auto *currentGroup = new QGroupBox("Current capture");
  auto *currentLayout = new QVBoxLayout(currentGroup);
  m_currentImage = new ImageCanvas;
  currentLayout->addWidget(m_currentImage);
  sideLayout->addWidget(baselineGroup, 1);
  sideLayout->addWidget(currentGroup, 1);
  viewerTabs->addTab(sideBySide, "Side by side");

  auto *overlayPage = new QWidget;
  auto *overlayLayout = new QVBoxLayout(overlayPage);
  m_overlayImage = new OverlayCanvas;
  overlayLayout->addWidget(m_overlayImage, 1);
  auto *opacityRow = new QHBoxLayout;
  opacityRow->addWidget(new QLabel("Current image opacity"));
  auto *opacity = new QSlider(Qt::Horizontal);
  opacity->setRange(0, 100);
  opacity->setValue(50);
  opacityRow->addWidget(opacity, 1);
  overlayLayout->addLayout(opacityRow);
  viewerTabs->addTab(overlayPage, "Overlay");

  m_diffImage = new ImageCanvas;
  viewerTabs->addTab(m_diffImage, "Difference");

  auto *baselinePage = new QWidget;
  auto *baselinePageLayout = new QVBoxLayout(baselinePage);
  baselinePageLayout->addWidget(helperText("Baselines are project-owned reference images. Removing one stops future matching for that exact URL, browser, viewport, mode, and format."));
  m_baselines = new QTableWidget(0, 4);
  m_baselines->setHorizontalHeaderLabels({"Target", "Browser / Viewport", "Updated", "File"});
  m_baselines->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_baselines->setSelectionMode(QAbstractItemView::SingleSelection);
  m_baselines->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_baselines->horizontalHeader()->setStretchLastSection(true);
  baselinePageLayout->addWidget(m_baselines, 1);
  auto *baselineButtons = new QHBoxLayout;
  auto *removeBaseline = new QPushButton("Remove Selected Baseline");
  baselineButtons->addWidget(removeBaseline);
  baselineButtons->addStretch();
  baselinePageLayout->addLayout(baselineButtons);
  viewerTabs->addTab(baselinePage, "Baselines");

  m_compareSplit->addWidget(listPanel);
  m_compareSplit->addWidget(viewerTabs);
  m_compareSplit->setStretchFactor(0, 2);
  m_compareSplit->setStretchFactor(1, 5);
  m_compareSplit->setSizes({460, 700});
  layout->addWidget(m_compareSplit, 1);
  connect(m_comparisons, &QTableWidget::itemSelectionChanged, this, &MainWindow::showSelectedComparison);
  connect(opacity, &QSlider::valueChanged, m_overlayImage, &OverlayCanvas::setOpacity);
  connect(refresh, &QPushButton::clicked, this, [this] { refreshComparisons(); refreshBaselines(); });
  connect(history, &QPushButton::clicked, this, [this] {
    m_tabs->setCurrentIndex(1);
  });
  connect(removeBaseline, &QPushButton::clicked, this, [this] {
    const int row = m_baselines->currentRow();
    if (row < 0) return;
    const QString key = m_baselines->item(row, 0)->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, "Remove baseline", "Remove the selected baseline reference?") != QMessageBox::Yes) return;
    rpcCall("baseline.remove", {{"projectId", m_projectId}, {"comparisonKey", key}},
            [this](const QJsonObject &) { refreshBaselines(); });
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
  auto *edit = new QPushButton("Edit");
  auto *toggle = new QPushButton("Enable / Disable");
  auto *run = new QPushButton("Run Now");
  auto *remove = new QPushButton("Remove");
  auto *refresh = new QPushButton("Refresh");
  explain(add, "Create a recurring capture using the profile currently selected on Capture.");
  explain(run, "Run the selected schedule immediately.");
  explain(remove, "Permanently remove the selected schedule after confirmation.");
  for (auto *button : {add, edit, toggle, run, remove, refresh}) buttons->addWidget(button);
  buttons->addStretch();
  layout->addLayout(buttons);
  connect(add, &QPushButton::clicked, this, &MainWindow::createSchedule);
  connect(edit, &QPushButton::clicked, this, [this] {
    const QString id = selectedScheduleId();
    for (const auto &value : m_schedulesCache) if (value.toObject().value("id").toString() == id) editSchedule(value.toObject());
  });
  connect(toggle, &QPushButton::clicked, this, [this] {
    const QString id = selectedScheduleId();
    for (const auto &value : m_schedulesCache) {
      QJsonObject schedule = value.toObject();
      if (schedule.value("id").toString() != id) continue;
      const bool enabling = !schedule.value("enabled").toBool();
      schedule.insert("enabled", enabling);
      rpcCall("schedule.upsert", {{"projectId", m_projectId}, {"schedule", schedule}},
              [this, enabling](const QJsonObject &) { refreshSchedules(); if (enabling) promptForAutostart(); });
      break;
    }
  });
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
  auto *networkGroup = new QGroupBox("Project network access");
  auto *networkLayout = new QVBoxLayout(networkGroup);
  m_allowLocalhost = new QCheckBox("Allow this project to capture localhost");
  explain(m_allowLocalhost, "Allows loopback addresses such as localhost and 127.0.0.1 for this project only. Private LAN addresses remain blocked.");
  networkLayout->addWidget(helperText("Public HTTP and HTTPS sites are allowed by default. Enable this only when the active project must capture a development server running on this computer."));
  networkLayout->addWidget(m_allowLocalhost);
  layout->addWidget(networkGroup);
  auto *scheduleGroup = new QGroupBox("Background schedules");
  auto *scheduleLayout = new QVBoxLayout(scheduleGroup);
  m_launchAtLogin = new QCheckBox("Start the CyberSnapper agent when I sign in");
  explain(m_launchAtLogin, "Keeps scheduled captures available after login even when the main window is closed.");
  scheduleLayout->addWidget(helperText("Scheduled captures only run while the background agent is running."));
  scheduleLayout->addWidget(m_launchAtLogin);
  layout->addWidget(scheduleGroup);
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
  auto *browserCards = new QWidget;
  auto *browserLayout = new QGridLayout(browserCards);
  browserLayout->setContentsMargins(0, 0, 0, 0);
  auto *installChromium = new QPushButton("Install Chromium");
  auto *installFirefox = new QPushButton("Install Firefox");
  auto *installWebKit = new QPushButton("Install WebKit");
  explain(installChromium, "Install or repair CyberSnapper’s managed Chromium browser engine.");
  explain(installFirefox, "Install or repair CyberSnapper’s managed Firefox browser engine.");
  explain(installWebKit, "Install or repair CyberSnapper’s managed WebKit browser engine.");
  const QList<QPair<QString, QPushButton *>> browserRows{{"chromium", installChromium},
                                                         {"firefox", installFirefox},
                                                         {"webkit", installWebKit}};
  for (int row = 0; row < browserRows.size(); ++row) {
    const QString engine = browserRows.at(row).first;
    auto *status = new QLabel("Checking…");
    status->setMinimumWidth(100);
    m_browserStatuses.insert(engine, status);
    browserLayout->addWidget(new QLabel(engine.at(0).toUpper() + engine.mid(1)), row, 0);
    browserLayout->addWidget(status, row, 1);
    browserLayout->addWidget(browserRows.at(row).second, row, 2);
  }
  browserLayout->setColumnStretch(1, 1);
  runtimeForm->addRow(helperText("The capture engine runs browser automation in the background. Browser installation state is available by hovering over Engine status."));
  runtimeForm->addRow("Engine status", m_workerStatus);
  runtimeForm->addRow("Browser engines", browserCards);
  runtimeForm->addRow("Simultaneous jobs", m_maximumJobs);
  runtimeForm->addRow(QString(), saveRuntime);
  layout->addWidget(runtimeGroup);
  layout->addStretch();
  connect(m_allowLocalhost, &QCheckBox::toggled, this, [this](bool allowed) {
    if (!m_rpc.isConnected() || m_projectId.isEmpty()) return;
    if (allowed && QMessageBox::warning(this, "Allow localhost capture",
        "This project will be allowed to load services on this computer through localhost. Private LAN addresses remain blocked. Continue?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
      m_allowLocalhost->blockSignals(true); m_allowLocalhost->setChecked(false); m_allowLocalhost->blockSignals(false);
      return;
    }
    rpcCall("project.settings.set", {{"projectId", m_projectId}, {"allowLocalhost", allowed}},
            [this](const QJsonObject &) { statusBar()->showMessage("Project network policy saved", 3000); });
  });
  connect(m_launchAtLogin, &QCheckBox::toggled, this, [this](bool enabled) {
    if (!m_rpc.isConnected()) return;
    rpcCall("autostart.set", {{"enabled", enabled}}, [this](const QJsonObject &) {
      statusBar()->showMessage("Login startup setting saved", 3000);
    });
  });
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
    <p>A profile is a reusable set of capture options. The Capture page marks unsaved edits and offers Save, Revert, and Save As. <b>Manage</b> opens the complete tabbed editor for viewports, output naming, collision behavior, timeouts, compression, PDF, blocking, and comparison settings.</p>

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
    <p>Select a non-PDF file in History and choose <b>Set as Baseline</b>. Enable comparison in a capture profile. Future captures are matched by URL, browser, viewport, mode, and format. Compare provides side-by-side, adjustable overlay, generated-difference, and baseline-manager views.</p>
    <p><b>Pixel sensitivity</b> decides how large a color change must be before a pixel counts as changed. <b>Allowed difference</b> is the percentage of changed pixels permitted before the result is marked different. Dynamic-element selectors are hidden in comparison-enabled captures to stabilize timestamps, ads, and rotating content.</p>

    <h2>History, schedules, and settings</h2>
    <p><b>History</b> filters jobs and shows files plus failure details. <b>Schedules</b> supports once, interval, daily, multi-day weekly, and monthly recurrences in an IANA time zone. <b>Settings</b> installs browser engines, controls login startup and simultaneous jobs, and optionally enables the authenticated localhost API.</p>

    <h2>Network access</h2>
    <p>Public HTTP(S) destinations are allowed by default. Settings can allow localhost for the active project when capturing a development server on this computer. Private LAN addresses remain blocked.</p>

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

void MainWindow::restoreUiState() {
  QSettings settings("CyberBrand", "CyberSnapper");
  restoreGeometry(settings.value("ui/geometry").toByteArray());
  restoreState(settings.value("ui/windowState").toByteArray());
  if (m_tabs) m_tabs->setCurrentIndex(qBound(0, settings.value("ui/mainTab", 0).toInt(), m_tabs->count() - 1));
  const auto restoreSplitter = [&settings](QSplitter *splitter, const char *key) {
    const QByteArray state = settings.value(QString::fromLatin1(key)).toByteArray();
    if (splitter && !state.isEmpty()) splitter->restoreState(state);
  };
  restoreSplitter(m_captureVertical, "ui/captureVertical");
  restoreSplitter(m_captureColumns, "ui/captureColumns");
  restoreSplitter(m_historySplit, "ui/historySplit");
  restoreSplitter(m_compareSplit, "ui/compareSplit");
  const auto restoreHeader = [&settings](QTableWidget *table, const char *key) {
    const QByteArray state = settings.value(QString::fromLatin1(key)).toByteArray();
    if (table && !state.isEmpty()) table->horizontalHeader()->restoreState(state);
  };
  restoreHeader(m_history, "ui/historyHeader");
  restoreHeader(m_artifacts, "ui/artifactsHeader");
  restoreHeader(m_comparisons, "ui/comparisonsHeader");
}

void MainWindow::saveUiState() const {
  QSettings settings("CyberBrand", "CyberSnapper");
  settings.setValue("ui/geometry", saveGeometry());
  settings.setValue("ui/windowState", saveState());
  if (m_tabs) settings.setValue("ui/mainTab", m_tabs->currentIndex());
  const auto saveSplitter = [&settings](QSplitter *splitter, const char *key) {
    if (splitter) settings.setValue(QString::fromLatin1(key), splitter->saveState());
  };
  saveSplitter(m_captureVertical, "ui/captureVertical");
  saveSplitter(m_captureColumns, "ui/captureColumns");
  saveSplitter(m_historySplit, "ui/historySplit");
  saveSplitter(m_compareSplit, "ui/compareSplit");
  const auto saveHeader = [&settings](QTableWidget *table, const char *key) {
    if (table) settings.setValue(QString::fromLatin1(key), table->horizontalHeader()->saveState());
  };
  saveHeader(m_history, "ui/historyHeader");
  saveHeader(m_artifacts, "ui/artifactsHeader");
  saveHeader(m_comparisons, "ui/comparisonsHeader");
}

void MainWindow::showFirstRun() {
  if (!qEnvironmentVariable("CYBERSNAPPER_UI_SCREENSHOT").isEmpty()) return;
  QSettings settings("CyberBrand", "CyberSnapper");
  if (settings.value("onboarding/completed", false).toBool()) return;
  QWizard wizard(this);
  wizard.setWindowTitle("Welcome to CyberSnapper");
  wizard.resize(620, 420);
  auto page = [](const QString &title, const QString &body) {
    auto *result = new QWizardPage; result->setTitle(title);
    auto *layout = new QVBoxLayout(result); auto *text = new QLabel(body); text->setWordWrap(true);
    layout->addWidget(text); layout->addStretch(); return result;
  };
  wizard.addPage(page("Projects keep work portable",
      "Every capture belongs to a project folder containing profiles, history, schedules, baselines, and output files. Quick Captures is ready now; use New or Open in the header for other projects."));
  wizard.addPage(page("Build a capture plan",
      "Enter one URL per line, choose a saved profile, and adjust the visible options. The plan summary shows the exact output count and blocks invalid combinations before they reach the queue."));
  wizard.addPage(page("Browsers and scheduled work",
      "Chromium, Firefox, and WebKit are managed separately in Settings. When you enable your first schedule, CyberSnapper will ask whether its background agent should start at login."));
  if (wizard.exec() == QDialog::Accepted) settings.setValue("onboarding/completed", true);
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
  refreshSettings();
}

void MainWindow::scheduleRefresh() {
  if (m_refreshPending) return;
  m_refreshPending = true;
  QTimer::singleShot(150, this, [this] {
    m_refreshPending = false;
    refreshJobs();
    refreshSchedules();
    refreshComparisons();
    refreshBaselines();
  });
}

void MainWindow::refreshProjects() {
  rpcCall("project.list", {}, [this](const QJsonObject &result) {
    m_projects = result.value("projects").toArray();
    const QString activeProject = result.value("activeProjectId").toString();
    if (!m_projectId.isEmpty() && m_projectId != activeProject) {
      m_profileDirty = false;
      m_loadedProfileId.clear();
    }
    m_projectId = activeProject;
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
    refreshComparisons();
    refreshBaselines();
    refreshSettings();
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
    m_jobsCache = result.value("jobs").toArray();
    m_activeJobs->clear();
    for (const auto &value : m_jobsCache) {
      const QJsonObject job = value.toObject();
      const QString id = job.value("id").toString();
      if (!QStringList{"succeeded", "partial", "failed", "cancelled", "interrupted"}.contains(job.value("status").toString())) {
        auto *active = new QTreeWidgetItem({id.left(8), job.value("status").toString(),
                                            QString::number(job.value("completedArtifacts").toInt()),
                                            QString::number(job.value("failedArtifacts").toInt()),
                                            displayTime(job.value("startedAt").toString())});
        active->setData(0, Qt::UserRole, id);
        m_activeJobs->addTopLevelItem(active);
      }
    }
    applyHistoryFilters();
  });
}

void MainWindow::applyHistoryFilters() {
  if (!m_history) return;
  const QString selected = selectedJobId();
  const QString search = m_historySearch ? m_historySearch->text().trimmed() : QString{};
  const QString status = m_historyStatus && m_historyStatus->currentIndex() > 0
      ? m_historyStatus->currentText() : QString{};
  const QString source = m_historySource && m_historySource->currentIndex() > 0
      ? m_historySource->currentText() : QString{};
  m_history->setRowCount(0);
  for (const auto &value : m_jobsCache) {
    const QJsonObject job = value.toObject();
    const QString jobStatus = job.value("status").toString();
    const QString jobSource = job.value("source").toString();
    const QString haystack = job.value("id").toString() + " " + job.value("error").toString() + " " +
        QString::fromUtf8(QJsonDocument(job.value("request").toObject()).toJson(QJsonDocument::Compact));
    if (!status.isEmpty() && jobStatus != status) continue;
    if (!source.isEmpty() && (source == "schedule" ? !jobSource.startsWith("schedule") : jobSource != source)) continue;
    if (!search.isEmpty() && !haystack.contains(search, Qt::CaseInsensitive)) continue;
    const int row = m_history->rowCount();
    m_history->insertRow(row);
    const QString id = job.value("id").toString();
    m_history->setItem(row, 0, item(displayTime(job.value("createdAt").toString()), id));
    m_history->setItem(row, 1, item(jobStatus));
    m_history->setItem(row, 2, item(jobSource));
    m_history->setItem(row, 3, item(QString::number(job.value("completedArtifacts").toInt())));
    m_history->setItem(row, 4, item(QString::number(job.value("failedArtifacts").toInt())));
    m_history->setItem(row, 5, item(id));
    if (id == selected) m_history->selectRow(row);
  }
}

void MainWindow::refreshComparisons() {
  if (m_projectId.isEmpty() || !m_comparisons) return;
  rpcCall("comparison.list", {{"projectId", m_projectId}}, [this](const QJsonObject &result) {
    m_comparisonsCache = result.value("comparisons").toArray();
    m_comparisons->setRowCount(m_comparisonsCache.size());
    for (int row = 0; row < m_comparisonsCache.size(); ++row) {
      const QJsonObject comparison = m_comparisonsCache.at(row).toObject();
      const QString id = comparison.value("id").toString();
      const QStringList parts = comparison.value("comparisonKey").toString().split('|');
      const QString target = parts.size() >= 3 ? parts.at(2) + " / " + parts.at(1) : "Unknown target";
      const QString url = parts.isEmpty() ? QString{} : parts.first();
      m_comparisons->setItem(row, 0, item(displayTime(comparison.value("createdAt").toString()), id));
      m_comparisons->setItem(row, 1, item(comparison.value("status").toString()));
      m_comparisons->setItem(row, 2, item(QString::number(comparison.value("mismatchRatio").toDouble() * 100.0, 'f', 3) + "%"));
      m_comparisons->setItem(row, 3, item(target));
      auto *urlItem = item(url); urlItem->setToolTip(url); m_comparisons->setItem(row, 4, urlItem);
    }
  });
}

void MainWindow::refreshBaselines() {
  if (m_projectId.isEmpty() || !m_baselines) return;
  rpcCall("baseline.list", {{"projectId", m_projectId}}, [this](const QJsonObject &result) {
    const QJsonArray baselines = result.value("baselines").toArray();
    m_baselines->setRowCount(baselines.size());
    for (int row = 0; row < baselines.size(); ++row) {
      const QJsonObject baseline = baselines.at(row).toObject();
      const QString key = baseline.value("comparisonKey").toString();
      const QStringList parts = key.split('|');
      auto *target = item(parts.isEmpty() ? key : parts.first(), key);
      target->setToolTip(key);
      m_baselines->setItem(row, 0, target);
      m_baselines->setItem(row, 1, item(parts.size() >= 3 ? parts.at(1) + " / " + parts.at(2) : QString{}));
      m_baselines->setItem(row, 2, item(displayTime(baseline.value("updatedAt").toString())));
      m_baselines->setItem(row, 3, item(baseline.value("relativePath").toString()));
    }
  });
}

void MainWindow::showSelectedComparison() {
  const int row = m_comparisons ? m_comparisons->currentRow() : -1;
  if (row < 0) return;
  const QString id = m_comparisons->item(row, 0)->data(Qt::UserRole).toString();
  rpcCall("comparison.resolve", {{"projectId", m_projectId}, {"comparisonId", id}}, [this](const QJsonObject &result) {
    const QString baseline = result.value("baselinePath").toString();
    const QString current = result.value("currentPath").toString();
    const QString diff = result.value("diffPath").toString();
    m_baselineImage->setImage(baseline);
    m_currentImage->setImage(current);
    m_overlayImage->setImages(baseline, current);
    m_diffImage->setImage(diff);
  });
}

void MainWindow::refreshSchedules() {
  if (m_projectId.isEmpty()) return;
  rpcCall("schedule.list", {{"projectId", m_projectId}}, [this](const QJsonObject &result) {
    m_schedulesCache = result.value("schedules").toArray();
    m_schedules->setRowCount(m_schedulesCache.size());
    for (int row = 0; row < m_schedulesCache.size(); ++row) {
      const auto schedule = m_schedulesCache.at(row).toObject();
      const auto recurrence = schedule.value("recurrence").toObject();
      QString recurrenceLabel = recurrence.value("type").toString();
      if (recurrenceLabel == "interval") recurrenceLabel = QStringLiteral("Every %1 min").arg(recurrence.value("minutes").toInt());
      else if (recurrenceLabel == "once") recurrenceLabel = "Once · " + displayTime(recurrence.value("at").toString());
      else recurrenceLabel = recurrenceLabel.left(1).toUpper() + recurrenceLabel.mid(1) + " · " + recurrence.value("time").toString();
      const QString id = schedule.value("id").toString();
      m_schedules->setItem(row, 0, item(schedule.value("name").toString(), id));
      m_schedules->setItem(row, 1, item(schedule.value("enabled").toBool() ? "Yes" : "No"));
      m_schedules->setItem(row, 2, item(recurrenceLabel));
      m_schedules->setItem(row, 3, item(displayTime(schedule.value("nextRun").toString())));
      m_schedules->setItem(row, 4, item(schedule.value("lastStatus").toString()));
      m_schedules->setItem(row, 5, item(id));
    }
  });
}

void MainWindow::refreshSettings() {
  if (m_allowLocalhost && !m_projectId.isEmpty()) {
    rpcCall("project.settings.get", {{"projectId", m_projectId}}, [this](const QJsonObject &result) {
      m_allowLocalhost->blockSignals(true);
      m_allowLocalhost->setChecked(result.value("allowLocalhost").toBool(false));
      m_allowLocalhost->blockSignals(false);
    });
  }
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
  if (m_launchAtLogin) {
    rpcCall("autostart.get", {}, [this](const QJsonObject &result) {
      m_launchAtLogin->blockSignals(true);
      m_launchAtLogin->setChecked(result.value("enabled").toBool(false));
      m_launchAtLogin->blockSignals(false);
    });
  }
  rpcCall("browser.status", {}, [this](const QJsonObject &result) {
    QStringList states;
    m_installedBrowsers.clear();
    const QJsonObject browsers = result.value("browsers").toObject();
    for (const auto &engine : {QString("chromium"), QString("firefox"), QString("webkit")}) {
      const bool installed = browsers.value(engine).toObject().value("installed").toBool();
      if (installed) m_installedBrowsers.insert(engine);
      states.append(engine + ": " + (installed ? "installed" : "not installed"));
      if (QLabel *label = m_browserStatuses.value(engine)) {
        label->setText(installed ? "Installed · Ready" : "Not installed");
        label->setStyleSheet(installed ? "color: palette(highlight); font-weight: 600;" : "color: palette(mid);");
      }
    }
    const QString workerEntry = m_workerStatus->property("workerEntry").toString();
    const QString worker = workerEntry.isEmpty() ? QString() : "Worker: " + workerEntry + "\n\n";
    m_workerStatus->setToolTip(worker + "Browser engines:\n" + states.join("\n"));
    updateCapturePlan();
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
  const QString requestedProfileId = m_profileCombo->currentData().toString();
  if (m_profileDirty && !m_loadedProfileId.isEmpty() && requestedProfileId != m_loadedProfileId) {
    if (QMessageBox::question(this, "Discard profile changes?",
        "The current profile has unsaved changes. Discard them and switch profiles?",
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) {
      m_profileCombo->blockSignals(true);
      m_profileCombo->setCurrentIndex(m_profileCombo->findData(m_loadedProfileId));
      m_profileCombo->blockSignals(false);
      return;
    }
  }
  QJsonObject profile;
  for (const auto &value : m_profiles) {
    if (value.toObject().value("id").toString() == m_profileCombo->currentData().toString()) profile = value.toObject();
  }
  if (profile.isEmpty() || !m_viewports) return;
  m_loadingProfile = true;
  m_viewports->blockSignals(true);
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
  m_viewports->blockSignals(false);
  m_loadingProfile = false;
  m_loadedProfileId = profile.value("id").toString();
  m_profileDirty = false;
  m_profileState->setText("Saved");
  m_profileState->setStyleSheet({});
  m_saveProfile->setEnabled(false);
  m_revertProfile->setEnabled(false);
  updateCapturePlan();
}

void MainWindow::markProfileDirty() {
  if (m_loadingProfile || !m_profileState) return;
  m_profileDirty = true;
  m_profileState->setText("Unsaved changes");
  m_profileState->setStyleSheet("color: palette(highlight); font-weight: 600;");
  m_saveProfile->setEnabled(true);
  m_revertProfile->setEnabled(true);
}

void MainWindow::saveCurrentProfile() {
  if (m_projectId.isEmpty() || m_profileCombo->currentData().toString().isEmpty()) return;
  QJsonObject profile = captureProfile();
  profile.insert("id", m_profileCombo->currentData().toString());
  profile.insert("name", m_profileCombo->currentText());
  rpcCall("profile.save", {{"projectId", m_projectId}, {"profile", profile}}, [this](const QJsonObject &) {
    m_profileDirty = false;
    statusBar()->showMessage("Profile saved", 3000);
    refreshProfiles();
  });
}

void MainWindow::updateCapturePlan() {
  if (!m_capturePlan || !m_startCapture || !m_viewports) return;
  QStringList urls;
  QStringList errors;
  for (const QString &line : m_urls->toPlainText().split('\n')) {
    const QString value = line.trimmed();
    if (value.isEmpty()) continue;
    urls.append(value);
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || !QStringList{"http", "https"}.contains(url.scheme().toLower()) || url.host().isEmpty()) {
      errors.append("Use explicit HTTP or HTTPS URLs");
    }
    const QString host = url.host().toLower();
    if ((host == "localhost" || host.endsWith(".localhost") || host.startsWith("127.") || host == "::1") &&
        (!m_allowLocalhost || !m_allowLocalhost->isChecked())) {
      errors.append("Enable localhost access in Settings for local development URLs");
    }
  }
  int enabledViewports = 0;
  bool mobileWithFirefox = false;
  for (int row = 0; row < m_viewports->rowCount(); ++row) {
    const bool enabled = m_viewports->item(row, 0) && m_viewports->item(row, 0)->checkState() == Qt::Checked;
    if (!enabled) continue;
    ++enabledViewports;
    mobileWithFirefox = mobileWithFirefox || (m_viewports->item(row, 5) && m_viewports->item(row, 5)->checkState() == Qt::Checked);
  }
  const QStringList engines = checkedValues({{m_chromium, "chromium"}, {m_firefox, "firefox"}, {m_webkit, "webkit"}});
  const QStringList formats = checkedValues({{m_png, "png"}, {m_webp, "webp"}, {m_avif, "avif"}, {m_pdf, "pdf"}});
  if (urls.isEmpty()) errors.append("Enter at least one URL");
  if (enabledViewports == 0) errors.append("Enable at least one viewport");
  if (engines.isEmpty()) errors.append("Select at least one browser");
  if (formats.isEmpty()) errors.append("Select at least one format");
  if (m_captureMode->currentData().toString() == "element" && m_elementSelector->text().trimmed().isEmpty()) {
    errors.append("Element mode needs a CSS selector");
  }
  if (formats.contains("pdf") && !engines.contains("chromium")) errors.append("PDF requires Chromium");
  for (const QString &engine : engines) {
    if (!m_installedBrowsers.isEmpty() && !m_installedBrowsers.contains(engine)) {
      errors.append(engine + " is not installed");
    }
  }
  qint64 formatsAcrossEngines = 0;
  for (const QString &engine : engines) {
    for (const QString &format : formats) if (format != "pdf" || engine == "chromium") ++formatsAcrossEngines;
  }
  const qint64 files = urls.size() * enabledViewports * formatsAcrossEngines;
  if (files > 10000) errors.append("The 10,000-file job limit is exceeded");
  errors.removeDuplicates();
  QString summary = QStringLiteral("Plan: %1 URL%2 · %3 viewport%4 · %5 browser%6 · %7 output file%8")
      .arg(urls.size()).arg(urls.size() == 1 ? "" : "s")
      .arg(enabledViewports).arg(enabledViewports == 1 ? "" : "s")
      .arg(engines.size()).arg(engines.size() == 1 ? "" : "s")
      .arg(files).arg(files == 1 ? "" : "s");
  if (mobileWithFirefox && engines.contains("firefox")) {
    summary += "\nNote: Firefox uses touch input but does not support Playwright's mobile-layout flag.";
  }
  if (!errors.isEmpty()) summary += "\nFix before capture: " + errors.join(" · ");
  m_capturePlan->setText(summary);
  m_startCapture->setEnabled(errors.isEmpty() && !m_projectId.isEmpty());
}

void MainWindow::openProfileManager() {
  QJsonObject source;
  for (const auto &value : m_profiles) {
    if (value.toObject().value("id").toString() == m_profileCombo->currentData().toString()) source = value.toObject();
  }
  if (source.isEmpty()) return;

  QDialog dialog(this);
  dialog.setWindowTitle("Manage Profile — " + source.value("name").toString());
  dialog.resize(780, 620);
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(helperText("All fields below are stored in the selected project. Save updates this profile; Duplicate creates an independent copy."));
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs, 1);

  auto *general = new QWidget;
  auto *generalForm = new QFormLayout(general);
  auto *name = new QLineEdit(source.value("name").toString());
  auto *mode = new QComboBox;
  mode->addItem("Full page", "fullPage"); mode->addItem("Viewport", "viewport"); mode->addItem("Element", "element");
  mode->setCurrentIndex(qMax(0, mode->findData(source.value("captureMode").toString("fullPage"))));
  auto *element = new QLineEdit(source.value("elementSelector").toString());
  auto *concurrency = new QSpinBox; concurrency->setRange(1, 10); concurrency->setValue(source.value("concurrency").toInt(1));
  generalForm->addRow("Profile name", name);
  generalForm->addRow("Capture mode", mode);
  generalForm->addRow("Element selector", element);
  generalForm->addRow("Parallel pages", concurrency);
  generalForm->addRow(helperText("Full page scrolls and captures the document; Viewport captures the visible rectangle; Element captures the first matching CSS element."));
  tabs->addTab(general, "General");

  auto *viewportPage = new QWidget;
  auto *viewportLayout = new QVBoxLayout(viewportPage);
  auto *viewports = new QTableWidget(0, 6);
  viewports->setHorizontalHeaderLabels({"Use", "Name", "Width", "Height", "Pixel ratio", "Mobile mode"});
  viewports->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  for (int column : {0, 2, 3, 4, 5}) viewports->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  const QJsonArray viewportValues = source.value("viewports").toArray();
  viewports->setRowCount(viewportValues.size());
  for (int row = 0; row < viewportValues.size(); ++row) {
    const QJsonObject viewport = viewportValues.at(row).toObject();
    auto *enabled = item(QString()); enabled->setCheckState(viewport.value("enabled").toBool(true) ? Qt::Checked : Qt::Unchecked);
    auto *viewportName = item(viewport.value("name").toString(), viewport.value("id").toString());
    auto *mobile = item(QString()); mobile->setCheckState(viewport.value("mobile").toBool() ? Qt::Checked : Qt::Unchecked);
    viewports->setItem(row, 0, enabled); viewports->setItem(row, 1, viewportName);
    viewports->setItem(row, 2, item(QString::number(viewport.value("width").toInt())));
    viewports->setItem(row, 3, item(QString::number(viewport.value("height").toInt())));
    viewports->setItem(row, 4, item(QString::number(viewport.value("deviceScaleFactor").toDouble(1.0))));
    viewports->setItem(row, 5, mobile);
  }
  viewportLayout->addWidget(viewports, 1);
  viewportLayout->addWidget(helperText("Pixel ratio controls output density, not responsive layout. Mobile mode adds touch/mobile viewport behavior; Firefox supports touch but not Playwright's mobile-layout flag."));
  auto *viewportButtons = new QHBoxLayout;
  auto *addViewport = new QPushButton("Add");
  auto *removeViewport = new QPushButton("Remove");
  viewportButtons->addWidget(addViewport); viewportButtons->addWidget(removeViewport); viewportButtons->addStretch();
  viewportLayout->addLayout(viewportButtons);
  tabs->addTab(viewportPage, "Viewports");

  auto *output = new QWidget;
  auto *outputForm = new QFormLayout(output);
  auto *browserRow = new QWidget; auto *browserLayout = new QHBoxLayout(browserRow); browserLayout->setContentsMargins(0, 0, 0, 0);
  auto *chromium = new QCheckBox("Chromium"); auto *firefox = new QCheckBox("Firefox"); auto *webkit = new QCheckBox("WebKit");
  auto *formatRow = new QWidget; auto *formatLayout = new QHBoxLayout(formatRow); formatLayout->setContentsMargins(0, 0, 0, 0);
  auto *png = new QCheckBox("PNG"); auto *webp = new QCheckBox("WebP"); auto *avif = new QCheckBox("AVIF"); auto *pdf = new QCheckBox("PDF");
  for (auto *box : {chromium, firefox, webkit}) browserLayout->addWidget(box); browserLayout->addStretch();
  for (auto *box : {png, webp, avif, pdf}) formatLayout->addWidget(box); formatLayout->addStretch();
  QStringList selectedEngines; for (const auto &value : source.value("engines").toArray()) selectedEngines.append(value.toString());
  chromium->setChecked(selectedEngines.contains("chromium")); firefox->setChecked(selectedEngines.contains("firefox")); webkit->setChecked(selectedEngines.contains("webkit"));
  QStringList selectedFormats; for (const auto &value : source.value("formats").toArray()) selectedFormats.append(value.toString());
  png->setChecked(selectedFormats.contains("png")); webp->setChecked(selectedFormats.contains("webp")); avif->setChecked(selectedFormats.contains("avif")); pdf->setChecked(selectedFormats.contains("pdf"));
  auto *naming = new QLineEdit(source.value("namingTemplate").toString("{hostname}-{preset}"));
  auto *collision = new QComboBox; collision->addItem("Create a version", "version"); collision->addItem("Overwrite", "overwrite"); collision->addItem("Skip", "skip");
  collision->setCurrentIndex(qMax(0, collision->findData(source.value("collisionPolicy").toString("version"))));
  auto *webpQuality = new QSpinBox; webpQuality->setRange(1, 100); webpQuality->setValue(source.value("webpQuality").toInt(80));
  auto *avifQuality = new QSpinBox; avifQuality->setRange(1, 100); avifQuality->setValue(source.value("avifQuality").toInt(50));
  auto *pdfFormat = new QLineEdit(source.value("pdfFormat").toString("A4"));
  auto *pdfLandscape = new QCheckBox("Landscape"); pdfLandscape->setChecked(source.value("pdfLandscape").toBool(false));
  auto *pdfMargin = new QLineEdit(source.value("pdfMargin").toString("0"));
  outputForm->addRow("Browsers", browserRow); outputForm->addRow("Formats", formatRow);
  outputForm->addRow("Filename template", naming); outputForm->addRow("Existing file", collision);
  outputForm->addRow("WebP quality", webpQuality); outputForm->addRow("AVIF quality", avifQuality);
  outputForm->addRow("PDF paper format", pdfFormat); outputForm->addRow(QString(), pdfLandscape); outputForm->addRow("PDF margin", pdfMargin);
  outputForm->addRow(helperText("Template tokens include {hostname}, {path}, {preset}, {width}, {height}, {engine}, {date}, {time}, {job}, and {index}. Use / to create subfolders."));
  tabs->addTab(output, "Browsers & Output");

  auto *preparation = new QWidget;
  auto *preparationForm = new QFormLayout(preparation);
  const auto seconds = [&source](const char *key, double fallback) {
    auto *field = new QDoubleSpinBox; field->setRange(0, 300); field->setSuffix(" s"); field->setValue(source.value(key).toDouble(fallback)); return field;
  };
  auto *initialDelay = seconds("initialDelay", 1.5); auto *scrollDelay = seconds("scrollDelay", 1.8); auto *finalDelay = seconds("finalDelay", 1.0);
  auto *navigationTimeout = new QSpinBox; navigationTimeout->setRange(1, 600); navigationTimeout->setSuffix(" s"); navigationTimeout->setValue(source.value("navigationTimeoutSeconds").toInt(60));
  auto *selectorTimeout = new QSpinBox; selectorTimeout->setRange(1, 300); selectorTimeout->setSuffix(" s"); selectorTimeout->setValue(source.value("selectorTimeoutSeconds").toInt(30));
  auto *maxScroll = new QSpinBox; maxScroll->setRange(5, 1800); maxScroll->setSuffix(" s"); maxScroll->setValue(source.value("maxScrollSeconds").toInt(120));
  auto *maxHeight = new QSpinBox; maxHeight->setRange(1000, 1000000); maxHeight->setSuffix(" px"); maxHeight->setValue(source.value("maxPageHeight").toInt(100000));
  auto *blockPopups = new QCheckBox("Hide common overlays"); blockPopups->setChecked(source.value("blockPopups").toBool());
  auto *stripWhitespace = new QCheckBox("Trim blank space at the top"); stripWhitespace->setChecked(source.value("stripWhitespace").toBool(true));
  auto *waitSelector = new QLineEdit(source.value("waitForSelector").toString());
  const auto joined = [&source](const char *key) { QStringList values; for (const auto &v : source.value(key).toArray()) values.append(v.toString()); return values.join(", "); };
  auto *hideSelectors = new QLineEdit(joined("hideSelectors"));
  auto *blocklist = new QLineEdit(joined("blocklist"));
  preparationForm->addRow("After load", initialDelay); preparationForm->addRow("After scroll", scrollDelay); preparationForm->addRow("Before capture", finalDelay);
  preparationForm->addRow("Navigation timeout", navigationTimeout); preparationForm->addRow("Selector timeout", selectorTimeout);
  preparationForm->addRow("Maximum auto-scroll", maxScroll); preparationForm->addRow("Maximum page height", maxHeight);
  preparationForm->addRow(QString(), blockPopups); preparationForm->addRow(QString(), stripWhitespace);
  preparationForm->addRow("Wait for selector", waitSelector); preparationForm->addRow("Hide selectors", hideSelectors); preparationForm->addRow("Block URL fragments", blocklist);
  tabs->addTab(preparation, "Page Preparation");

  auto *comparison = new QWidget;
  auto *comparisonForm = new QFormLayout(comparison);
  auto *comparisonEnabled = new QCheckBox("Compare captures with saved baselines"); comparisonEnabled->setChecked(source.value("comparisonEnabled").toBool());
  auto *pixelThreshold = new QDoubleSpinBox; pixelThreshold->setRange(0, 100); pixelThreshold->setDecimals(2); pixelThreshold->setSuffix(" %"); pixelThreshold->setValue(source.value("pixelThreshold").toDouble(.1) * 100);
  auto *mismatchThreshold = new QDoubleSpinBox; mismatchThreshold->setRange(0, 100); mismatchThreshold->setDecimals(3); mismatchThreshold->setSuffix(" %"); mismatchThreshold->setValue(source.value("mismatchThreshold").toDouble(.001) * 100);
  auto *ignoreSelectors = new QLineEdit(joined("comparisonIgnoreSelectors"));
  comparisonForm->addRow(QString(), comparisonEnabled); comparisonForm->addRow("Pixel sensitivity", pixelThreshold);
  comparisonForm->addRow("Allowed difference", mismatchThreshold); comparisonForm->addRow("Hide dynamic selectors", ignoreSelectors);
  comparisonForm->addRow(helperText("Pixel sensitivity controls how different one pixel must be to count. Allowed difference controls how many changed pixels mark the entire capture as changed."));
  tabs->addTab(comparison, "Comparison");

  connect(addViewport, &QPushButton::clicked, &dialog, [viewports] {
    const int row = viewports->rowCount(); viewports->insertRow(row);
    auto *enabled = item(QString()); enabled->setCheckState(Qt::Checked);
    auto *mobile = item(QString()); mobile->setCheckState(Qt::Unchecked);
    viewports->setItem(row, 0, enabled); viewports->setItem(row, 1, item("Custom", newId()));
    viewports->setItem(row, 2, item("1440")); viewports->setItem(row, 3, item("900"));
    viewports->setItem(row, 4, item("1")); viewports->setItem(row, 5, mobile);
  });
  connect(removeViewport, &QPushButton::clicked, &dialog, [viewports] {
    if (viewports->currentRow() >= 0 && viewports->rowCount() > 1) viewports->removeRow(viewports->currentRow());
  });

  auto buildProfile = [&]() {
    QJsonObject profile = source;
    profile.insert("name", name->text().trimmed()); profile.insert("captureMode", mode->currentData().toString());
    profile.insert("elementSelector", element->text().trimmed()); profile.insert("concurrency", concurrency->value());
    profile.insert("engines", stringArray(checkedValues({{chromium, "chromium"}, {firefox, "firefox"}, {webkit, "webkit"}})));
    profile.insert("formats", stringArray(checkedValues({{png, "png"}, {webp, "webp"}, {avif, "avif"}, {pdf, "pdf"}})));
    profile.insert("namingTemplate", naming->text().trimmed()); profile.insert("collisionPolicy", collision->currentData().toString());
    profile.insert("webpQuality", webpQuality->value()); profile.insert("avifQuality", avifQuality->value());
    profile.insert("pdfFormat", pdfFormat->text().trimmed()); profile.insert("pdfLandscape", pdfLandscape->isChecked()); profile.insert("pdfMargin", pdfMargin->text().trimmed());
    profile.insert("initialDelay", initialDelay->value()); profile.insert("scrollDelay", scrollDelay->value()); profile.insert("finalDelay", finalDelay->value());
    profile.insert("navigationTimeoutSeconds", navigationTimeout->value()); profile.insert("selectorTimeoutSeconds", selectorTimeout->value());
    profile.insert("maxScrollSeconds", maxScroll->value()); profile.insert("maxPageHeight", maxHeight->value());
    profile.insert("blockPopups", blockPopups->isChecked()); profile.insert("stripWhitespace", stripWhitespace->isChecked());
    profile.insert("waitForSelector", waitSelector->text().trimmed());
    const auto commaValues = [](const QString &text) { QStringList values; for (const QString &part : text.split(',')) if (!part.trimmed().isEmpty()) values.append(part.trimmed()); return stringArray(values); };
    profile.insert("hideSelectors", commaValues(hideSelectors->text())); profile.insert("blocklist", commaValues(blocklist->text()));
    profile.insert("comparisonEnabled", comparisonEnabled->isChecked()); profile.insert("pixelThreshold", pixelThreshold->value() / 100.0);
    profile.insert("mismatchThreshold", mismatchThreshold->value() / 100.0); profile.insert("comparisonIgnoreSelectors", commaValues(ignoreSelectors->text()));
    QJsonArray rows;
    for (int row = 0; row < viewports->rowCount(); ++row) {
      rows.append(QJsonObject{{"id", viewports->item(row, 1)->data(Qt::UserRole).toString()}, {"name", viewports->item(row, 1)->text()},
                              {"width", viewports->item(row, 2)->text().toInt()}, {"height", viewports->item(row, 3)->text().toInt()},
                              {"deviceScaleFactor", viewports->item(row, 4)->text().toDouble()},
                              {"mobile", viewports->item(row, 5)->checkState() == Qt::Checked},
                              {"enabled", viewports->item(row, 0)->checkState() == Qt::Checked}});
    }
    profile.insert("viewports", rows);
    return profile;
  };
  auto validate = [&](const QJsonObject &profile) {
    if (profile.value("name").toString().isEmpty()) return QString("Profile name is required");
    if (profile.value("engines").toArray().isEmpty()) return QString("Select at least one browser");
    if (profile.value("formats").toArray().isEmpty()) return QString("Select at least one format");
    QStringList profileEngines; for (const auto &value : profile.value("engines").toArray()) profileEngines.append(value.toString());
    QStringList profileFormats; for (const auto &value : profile.value("formats").toArray()) profileFormats.append(value.toString());
    if (profileFormats.contains("pdf") && !profileEngines.contains("chromium")) return QString("PDF output requires Chromium");
    if (profile.value("captureMode").toString() == "element" && profile.value("elementSelector").toString().isEmpty()) return QString("Element mode needs a CSS selector");
    bool enabled = false; for (const auto &value : profile.value("viewports").toArray()) enabled = enabled || value.toObject().value("enabled").toBool();
    if (!enabled) return QString("Enable at least one viewport");
    return QString{};
  };

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  auto *duplicate = buttons->addButton("Duplicate", QDialogButtonBox::ActionRole);
  auto *remove = buttons->addButton("Delete", QDialogButtonBox::DestructiveRole);
  remove->setEnabled(source.value("id").toString() != "default");
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this] {
    const QJsonObject profile = buildProfile(); const QString error = validate(profile);
    if (!error.isEmpty()) { QMessageBox::information(&dialog, "Profile needs attention", error); return; }
    rpcCall("profile.save", {{"projectId", m_projectId}, {"profile", profile}}, [&, this](const QJsonObject &) { dialog.accept(); refreshProfiles(); });
  });
  connect(duplicate, &QPushButton::clicked, &dialog, [&, this] {
    QJsonObject profile = buildProfile(); const QString error = validate(profile);
    if (!error.isEmpty()) { QMessageBox::information(&dialog, "Profile needs attention", error); return; }
    profile.insert("id", newId()); profile.insert("name", profile.value("name").toString() + " copy");
    rpcCall("profile.save", {{"projectId", m_projectId}, {"profile", profile}}, [&, this](const QJsonObject &) { dialog.accept(); refreshProfiles(); });
  });
  connect(remove, &QPushButton::clicked, &dialog, [&, this] {
    if (QMessageBox::question(&dialog, "Delete profile", "Delete this profile? Schedules using it must be changed first.") != QMessageBox::Yes) return;
    rpcCall("profile.remove", {{"projectId", m_projectId}, {"profileId", source.value("id").toString()}},
            [&, this](const QJsonObject &) { dialog.accept(); refreshProfiles(); });
  });
  dialog.exec();
}

void MainWindow::submitCapture() {
  updateCapturePlan();
  if (!m_startCapture->isEnabled()) {
    QMessageBox::information(this, "Capture plan needs attention", m_capturePlan->text());
    return;
  }
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
    const QString error = job.value("error").toString();
    m_jobSummary->setText(QStringLiteral("%1 · started %2 · finished %3 · %4 file%5%6")
        .arg(job.value("status").toString(), displayTime(job.value("startedAt").toString()),
             displayTime(job.value("finishedAt").toString()))
        .arg(job.value("completedArtifacts").toInt() + job.value("failedArtifacts").toInt())
        .arg(job.value("completedArtifacts").toInt() + job.value("failedArtifacts").toInt() == 1 ? "" : "s")
        .arg(error.isEmpty() ? QString{} : " · " + error));
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
    refreshBaselines();
  });
}

void MainWindow::createSchedule() {
  editSchedule({});
}

void MainWindow::editSchedule(const QJsonObject &existing) {
  QDialog dialog(this);
  dialog.setWindowTitle(existing.isEmpty() ? "New Schedule" : "Edit Schedule");
  dialog.resize(620, 560);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  auto *name = new QLineEdit(existing.value("name").toString("Daily capture"));
  auto *enabled = new QCheckBox("Enabled"); enabled->setChecked(existing.value("enabled").toBool(true));
  auto *profile = new QComboBox;
  for (const auto &value : m_profiles) profile->addItem(value.toObject().value("name").toString(), value.toObject().value("id").toString());
  const int profileIndex = profile->findData(existing.value("profileId").toString(m_profileCombo->currentData().toString()));
  if (profileIndex >= 0) profile->setCurrentIndex(profileIndex);
  QString existingUrls;
  if (existing.isEmpty()) existingUrls = m_urls->toPlainText();
  else { QStringList values; for (const auto &value : existing.value("urls").toArray()) values.append(value.toString()); existingUrls = values.join('\n'); }
  auto *urls = new QTextEdit(existingUrls); urls->setMaximumHeight(110); urls->setAcceptRichText(false);
  const QJsonObject oldRecurrence = existing.value("recurrence").toObject();
  auto *type = new QComboBox;
  type->addItem("Once", "once"); type->addItem("Daily", "daily"); type->addItem("Weekly", "weekly");
  type->addItem("Monthly", "monthly"); type->addItem("Interval", "interval");
  type->setCurrentIndex(qMax(0, type->findData(oldRecurrence.value("type").toString("daily"))));
  auto *once = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1));
  once->setCalendarPopup(true); once->setDisplayFormat("yyyy-MM-dd HH:mm");
  const QDateTime oldOnce = QDateTime::fromString(oldRecurrence.value("at").toString(), Qt::ISODate);
  if (oldOnce.isValid()) once->setDateTime(oldOnce.toLocalTime());
  auto *time = new QTimeEdit(QTime::fromString(oldRecurrence.value("time").toString("09:00"), "HH:mm"));
  time->setDisplayFormat("HH:mm");
  auto *weekdays = new QWidget; auto *weekdayLayout = new QHBoxLayout(weekdays); weekdayLayout->setContentsMargins(0, 0, 0, 0);
  QList<QCheckBox *> weekdayBoxes;
  QSet<int> oldDays; for (const auto &value : oldRecurrence.value("daysOfWeek").toArray()) oldDays.insert(value.toInt());
  const QStringList dayNames{"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  for (int day = 1; day <= 7; ++day) {
    auto *box = new QCheckBox(dayNames.at(day - 1)); box->setChecked(oldDays.isEmpty() ? day == 1 : oldDays.contains(day));
    weekdayBoxes.append(box); weekdayLayout->addWidget(box);
  }
  auto *monthDay = new QComboBox;
  for (int day = 1; day <= 31; ++day) monthDay->addItem(QString::number(day), day);
  monthDay->addItem("Last day", "last");
  const QJsonValue oldMonthDay = oldRecurrence.value("day");
  int monthIndex = oldMonthDay.isString() ? monthDay->findData(oldMonthDay.toString()) : monthDay->findData(oldMonthDay.toInt(1));
  if (monthIndex >= 0) monthDay->setCurrentIndex(monthIndex);
  auto *interval = new QSpinBox; interval->setRange(15, 525600); interval->setValue(60); interval->setSuffix(" min");
  interval->setValue(oldRecurrence.value("minutes").toInt(60));
  auto *zone = new QComboBox; zone->setEditable(true);
  const QList<QByteArray> zones = QTimeZone::availableTimeZoneIds();
  for (const QByteArray &id : zones) zone->addItem(QString::fromUtf8(id), QString::fromUtf8(id));
  const QString oldZone = oldRecurrence.value("timeZone").toString(QString::fromUtf8(QTimeZone::systemTimeZoneId()));
  const int zoneIndex = zone->findData(oldZone); if (zoneIndex >= 0) zone->setCurrentIndex(zoneIndex); else zone->setCurrentText(oldZone);
  form->addRow("Name", name); form->addRow(QString(), enabled); form->addRow("Profile", profile);
  form->addRow("URLs", urls); form->addRow("Recurrence", type); form->addRow("Run once at", once);
  form->addRow("Time", time); form->addRow("Days", weekdays); form->addRow("Day of month", monthDay);
  form->addRow("Interval", interval); form->addRow("Time zone", zone);
  layout->addLayout(form);
  layout->addWidget(helperText("Weekly supports any combination of days. Monthly dates beyond a shorter month's end run on that month's final day. Once schedules disable themselves after running."));
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  const auto updateFields = [=] {
    const QString selected = type->currentData().toString();
    once->setEnabled(selected == "once"); time->setEnabled(QStringList{"daily", "weekly", "monthly"}.contains(selected));
    weekdays->setEnabled(selected == "weekly"); monthDay->setEnabled(selected == "monthly");
    interval->setEnabled(selected == "interval"); zone->setEnabled(selected != "interval");
  };
  connect(type, &QComboBox::currentIndexChanged, &dialog, updateFields);
  updateFields();
  if (dialog.exec() != QDialog::Accepted) return;
  QStringList urlList;
  for (const auto &line : urls->toPlainText().split('\n')) if (!line.trimmed().isEmpty()) urlList.append(line.trimmed());
  if (name->text().trimmed().isEmpty() || urlList.isEmpty()) {
    QMessageBox::information(this, "Schedule needs attention", "Enter a schedule name and at least one URL.");
    return;
  }
  QJsonObject recurrence;
  const QString recurrenceType = type->currentData().toString();
  if (recurrenceType == "once") {
    QTimeZone selectedZone(zone->currentText().toUtf8()); if (!selectedZone.isValid()) selectedZone = QTimeZone::systemTimeZone();
    recurrence = {{"type", "once"}, {"at", QDateTime(once->date(), once->time(), selectedZone).toUTC().toString(Qt::ISODateWithMs)}};
  } else if (recurrenceType == "interval") recurrence = {{"type", "interval"}, {"minutes", interval->value()}};
  else if (recurrenceType == "daily") recurrence = {{"type", "daily"}, {"time", time->time().toString("HH:mm")}};
  else if (recurrenceType == "weekly") {
    QJsonArray days; for (int day = 1; day <= 7; ++day) if (weekdayBoxes.at(day - 1)->isChecked()) days.append(day);
    if (days.isEmpty()) { QMessageBox::information(this, "Schedule needs attention", "Select at least one weekday."); return; }
    recurrence = {{"type", "weekly"}, {"time", time->time().toString("HH:mm")}, {"daysOfWeek", days}};
  } else recurrence = {{"type", "monthly"}, {"time", time->time().toString("HH:mm")}, {"day", QJsonValue::fromVariant(monthDay->currentData())}};
  if (recurrenceType != "interval" && recurrenceType != "once") recurrence.insert("timeZone", zone->currentText());
  QJsonObject schedule = existing;
  schedule.insert("name", name->text().trimmed()); schedule.insert("enabled", enabled->isChecked());
  schedule.insert("profileId", profile->currentData().toString()); schedule.insert("urls", stringArray(urlList)); schedule.insert("recurrence", recurrence);
  rpcCall("schedule.upsert", {{"projectId", m_projectId}, {"schedule", schedule}},
          [this, isEnabled = enabled->isChecked()](const QJsonObject &) {
    refreshSchedules(); if (isEnabled) promptForAutostart();
  });
}

void MainWindow::promptForAutostart() {
  QSettings settings("CyberBrand", "CyberSnapper");
  if (settings.value("schedules/autostartPrompted", false).toBool()) return;
  settings.setValue("schedules/autostartPrompted", true);
  rpcCall("autostart.get", {}, [this](const QJsonObject &result) {
    if (result.value("enabled").toBool()) return;
    if (QMessageBox::question(this, "Run schedules after login",
        "CyberSnapper must start in the background for schedules to run after you sign in. Start the CyberSnapper agent automatically at login?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
      rpcCall("autostart.set", {{"enabled", true}}, [this](const QJsonObject &) {
        statusBar()->showMessage("CyberSnapper will start at login", 4000);
      });
    }
  });
}

} // namespace CyberSnapper
