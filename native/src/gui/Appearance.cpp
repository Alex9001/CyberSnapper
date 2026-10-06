#include "gui/Appearance.h"

#include <QApplication>
#include <QFont>
#include <QProxyStyle>
#include <QPushButton>
#include <QWidget>
#include <cmath>

namespace CyberSnapper::Appearance {
namespace {

double relativeLuminance(const QColor &color) {
  const auto linear = [](double channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

QColor readableLabel(const QColor &preferred, const QColor &surface) {
  const double label = relativeLuminance(preferred) + 0.05;
  const double background = relativeLuminance(surface) + 0.05;
  if (qMax(label, background) / qMin(label, background) >= 4.5) return preferred;
  return background > 0.229 ? QColor(Qt::black) : QColor(Qt::white);
}

bool isCaptureControl(const QWidget *widget) {
  for (auto *parent = widget; parent; parent = parent->parentWidget()) {
    if (parent->objectName() == QStringLiteral("capturePage")) return true;
  }
  return false;
}

class ButtonStyle final : public QProxyStyle {
public:
  ButtonStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

  void polish(QWidget *widget) override {
    QProxyStyle::polish(widget);
    auto *button = qobject_cast<QPushButton *>(widget);
    if (!button) return;
    const QString role = button->objectName();
    if (role != QStringLiteral("primaryAction") && role != QStringLiteral("destructiveAction")) return;
    button->setPalette(buttonPalette(button->palette(), role));
    QFont font = button->font();
    font.setWeight(QFont::DemiBold);
    button->setFont(font);
  }

  QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                         const QSize &contents, const QWidget *widget) const override {
    QSize size = QProxyStyle::sizeFromContents(type, option, contents, widget);
    if (type != CT_PushButton || !widget) return size;
    const bool primary = widget->objectName() == QStringLiteral("primaryAction");
    const int minimumHeight = isCaptureControl(widget) ? 28 : 32;
    // Keep Qt's platform metrics, icon spacing and default-button allowance.
    return size.expandedTo(QSize(contents.width() + (primary ? 36 : 24), minimumHeight));
  }
};

void setDarkSurfaces(QPalette &palette) {
  palette.setColor(QPalette::Window, QColor(QStringLiteral("#101827")));
  palette.setColor(QPalette::Base, QColor(QStringLiteral("#09111f")));
  palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#162238")));
  palette.setColor(QPalette::Button, QColor(QStringLiteral("#223149")));
  palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#223149")));
  palette.setColor(QPalette::Light, QColor(QStringLiteral("#40526b")));
  palette.setColor(QPalette::Midlight, QColor(QStringLiteral("#30445f")));
  palette.setColor(QPalette::Mid, QColor(QStringLiteral("#53657a")));
  palette.setColor(QPalette::Dark, QColor(QStringLiteral("#101827")));
  palette.setColor(QPalette::Shadow, QColor(QStringLiteral("#050b14")));
}

void setDarkText(QPalette &palette) {
  const QColor text(QStringLiteral("#e8f5ff"));
  const QColor muted(QStringLiteral("#8fa5b8"));
  for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText}) {
    palette.setColor(role, text);
    palette.setColor(QPalette::Disabled, role, muted);
  }
  palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::PlaceholderText, muted);
  palette.setColor(QPalette::Link, QColor(QStringLiteral("#19bce8")));
  palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#19bce8")));
  palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#04111b")));
}

} // namespace

QPalette applicationPalette(const QPalette &systemPalette, bool dark) {
  QPalette palette = systemPalette;
  if (dark) {
    setDarkSurfaces(palette);
    setDarkText(palette);
  }
  return palette;
}

QPalette buttonPalette(const QPalette &palette, const QString &role) {
  QPalette adjusted = palette;
  for (const auto group : {QPalette::Active, QPalette::Inactive}) {
    if (role == QStringLiteral("primaryAction")) {
      adjusted.setColor(group, QPalette::Button, palette.color(group, QPalette::Highlight));
      adjusted.setColor(group, QPalette::ButtonText,
                        readableLabel(palette.color(group, QPalette::HighlightedText),
                                      palette.color(group, QPalette::Highlight)));
    } else if (role == QStringLiteral("destructiveAction")) {
      const bool dark = palette.color(group, QPalette::Button).lightness() < 128;
      adjusted.setColor(group, QPalette::ButtonText, QColor(dark ? "#ff8192" : "#b0253c"));
    }
  }
  // Disabled actions retain the ordinary button surface and muted label.
  return adjusted;
}

QString chromeStyleSheet() {
  // Do not style QPushButton backgrounds/borders: Fusion owns the bevel,
  // gradient, hover, pressed, keyboard-focus and default-button rendering.
  return QStringLiteral(R"(
    QGroupBox {
      font-weight: 600;
      margin-top: 8px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      left: 8px;
      padding: 0 4px;
    }
    QToolBar#mainNavigation {
      spacing: 2px;
      padding: 4px;
      border-bottom: 1px solid palette(mid);
    }
    QToolBar#mainNavigation QToolButton {
      padding: 6px 8px;
      margin: 1px;
      border: 1px solid transparent;
      border-radius: 5px;
    }
    QToolBar#mainNavigation QToolButton:hover {
      background-color: palette(midlight);
      border-color: palette(highlight);
    }
    QToolBar#mainNavigation QToolButton:checked {
      background-color: palette(highlight);
      color: palette(highlighted-text);
      font-weight: 600;
    }
    QLabel#helperText {
      background-color: palette(base);
      color: palette(text);
      border-left: 3px solid palette(highlight);
      padding: 5px 7px;
    }
    QLabel#pageTitle {
      font-size: 20px;
      font-weight: 700;
    }
    QLabel#metricValue {
      font-size: 24px;
      font-weight: 700;
      color: palette(highlight);
      padding: 4px 0;
    }
  )");
}

void apply(QApplication &application, bool dark) {
  const QPalette palette = applicationPalette(application.palette(), dark);
  application.setStyle(new ButtonStyle);
  application.setPalette(palette);
  application.setStyleSheet(chromeStyleSheet());
}

} // namespace CyberSnapper::Appearance
