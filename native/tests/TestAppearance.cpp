#include "gui/Appearance.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QTest>
#include <QVBoxLayout>
#include <cmath>

using namespace CyberSnapper;

namespace {

double brightness(const QColor &color) {
  const auto linear = [](double channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

double contrast(const QColor &first, const QColor &second) {
  const double a = brightness(first) + 0.05;
  const double b = brightness(second) + 0.05;
  return qMax(a, b) / qMin(a, b);
}

QImage renderButton(const QPushButton &button, QStyle::State state) {
  QImage image(180, 40, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QStyleOptionButton option;
  option.initFrom(&button);
  option.rect = image.rect();
  option.state = state;
  option.text = button.text();
  option.features = button.isDefault() ? QStyleOptionButton::DefaultButton : QStyleOptionButton::None;
  QPainter painter(&image);
  painter.setFont(button.font());
  button.style()->drawControl(QStyle::CE_PushButton, &option, &painter, &button);
  return image;
}

} // namespace

class TestAppearance final : public QObject {
  Q_OBJECT
private slots:
  void init();
  void semanticPalette_data();
  void semanticPalette();
  void nativeButtonStates_data();
  void nativeButtonStates();
  void disabledAction();
  void keyboardAndDefaultAction();
  void compactMetrics();
};

void TestAppearance::init() {
  // Begin with a known light system palette; the dark theme is applied explicitly.
  QScopedPointer<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
  qApp->setPalette(fusion->standardPalette());
  Appearance::apply(*qApp, true);
}

void TestAppearance::semanticPalette_data() {
  QTest::addColumn<bool>("dark");
  QTest::addColumn<QString>("role");
  QTest::newRow("light primary") << false << QStringLiteral("primaryAction");
  QTest::newRow("dark primary") << true << QStringLiteral("primaryAction");
  QTest::newRow("light destructive") << false << QStringLiteral("destructiveAction");
  QTest::newRow("dark destructive") << true << QStringLiteral("destructiveAction");
}

void TestAppearance::semanticPalette() {
  QFETCH(bool, dark);
  QFETCH(QString, role);
  QScopedPointer<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
  const auto base = Appearance::applicationPalette(fusion->standardPalette(), dark);
  const auto palette = Appearance::buttonPalette(base, role);
  for (const auto group : {QPalette::Active, QPalette::Inactive}) {
    QVERIFY(contrast(palette.color(group, QPalette::ButtonText), palette.color(group, QPalette::Button)) >= 4.5);
  }
  QCOMPARE(palette.color(QPalette::Disabled, QPalette::Button), base.color(QPalette::Disabled, QPalette::Button));
  QCOMPARE(palette.color(QPalette::Disabled, QPalette::ButtonText), base.color(QPalette::Disabled, QPalette::ButtonText));
}

void TestAppearance::nativeButtonStates_data() {
  QTest::addColumn<bool>("dark");
  QTest::newRow("light") << false;
  QTest::newRow("dark") << true;
}

void TestAppearance::nativeButtonStates() {
  QFETCH(bool, dark);
  QScopedPointer<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
  qApp->setPalette(fusion->standardPalette());
  Appearance::apply(*qApp, dark);
  QPushButton button(QStringLiteral("Start Capture"));
  button.setObjectName(QStringLiteral("primaryAction"));
  button.ensurePolished();
  QCOMPARE(button.palette().color(QPalette::Button), qApp->palette().color(QPalette::Highlight));
  QVERIFY(button.font().weight() >= QFont::DemiBold);
  QVERIFY(!Appearance::chromeStyleSheet().contains(QStringLiteral("QPushButton")));
  const auto normal = renderButton(button, QStyle::State_Enabled | QStyle::State_Raised);
  const auto hover = renderButton(button, QStyle::State_Enabled | QStyle::State_Raised | QStyle::State_MouseOver);
  const auto pressed = renderButton(button, QStyle::State_Enabled | QStyle::State_Sunken);
  const auto focused = renderButton(button, QStyle::State_Enabled | QStyle::State_Raised | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange);
  QVERIFY(normal != hover);
  QVERIFY(normal != pressed);
  QVERIFY(normal != focused);
  QVERIFY(normal.pixelColor(30, 8) != normal.pixelColor(30, 31)); // Fusion's dimensional fill.
  QVERIFY(contrast(button.palette().color(QPalette::ButtonText), normal.pixelColor(30, 8)) >= 4.5);
  QVERIFY(contrast(button.palette().color(QPalette::ButtonText), normal.pixelColor(30, 31)) >= 4.5);
  button.setDefault(true);
  QVERIFY(normal != renderButton(button, QStyle::State_Enabled | QStyle::State_Raised));
}

void TestAppearance::disabledAction() {
  QPushButton button(QStringLiteral("Start Capture"));
  button.setObjectName(QStringLiteral("primaryAction"));
  button.ensurePolished();
  QSignalSpy clicked(&button, &QPushButton::clicked);
  button.setEnabled(false);
  QCOMPARE(button.palette().color(QPalette::Button), qApp->palette().color(QPalette::Disabled, QPalette::Button));
  button.click();
  QCOMPARE(clicked.count(), 0);
}

void TestAppearance::keyboardAndDefaultAction() {
  QDialog dialog;
  auto *layout = new QVBoxLayout(&dialog);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  auto *save = buttons->button(QDialogButtonBox::Save);
  save->setObjectName(QStringLiteral("primaryAction"));
  save->setDefault(true);
  dialog.show();
  save->setFocus();
  QSignalSpy clicked(save, &QPushButton::clicked);
  QTest::keyClick(save, Qt::Key_Space);
  QTest::keyClick(&dialog, Qt::Key_Return);
  QCOMPARE(clicked.count(), 2);
  QVERIFY(save->isDefault());
  QVERIFY(save->focusPolicy() & Qt::TabFocus);
}

void TestAppearance::compactMetrics() {
  QWidget capture;
  capture.setObjectName(QStringLiteral("capturePage"));
  QPushButton compact(QStringLiteral("Configure…"), &capture);
  QPushButton ordinary(QStringLiteral("Configure…"));
  compact.ensurePolished();
  ordinary.ensurePolished();
  QCOMPARE(compact.sizeHint().height(), 28);
  QCOMPARE(ordinary.sizeHint().height(), 32);
  QVERIFY(compact.sizeHint().width() >= compact.fontMetrics().horizontalAdvance(compact.text()) + 24);
}

QTEST_MAIN(TestAppearance)
#include "TestAppearance.moc"
