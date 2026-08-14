#pragma once

#include <QString>

namespace CyberSnapper::Paths {

QString appConfigDir();
QString appDataDir();
QString cacheDir();
QString runtimeDir();
QString defaultProjectRoot();
QString agentServerName();
QString agentExecutable();
QString guiExecutable();
QString workerEntry();
QString nodeExecutable();
QString browserCacheDir();
bool ensureDirectory(const QString &path, QString *error = nullptr);

} // namespace CyberSnapper::Paths
