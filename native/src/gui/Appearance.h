#pragma once

#include <QPalette>
#include <QString>

class QApplication;

namespace CyberSnapper::Appearance {

// Buttons remain native Qt controls. Only semantic colors and spacing change.
QPalette applicationPalette(const QPalette &systemPalette, bool dark);
QPalette buttonPalette(const QPalette &palette, const QString &role);
QString chromeStyleSheet();
void apply(QApplication &application, bool dark);

} // namespace CyberSnapper::Appearance
