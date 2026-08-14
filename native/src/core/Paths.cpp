#include "core/Paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QStandardPaths>

namespace CyberSnapper::Paths {

namespace {

QString ensurePath(QStandardPaths::StandardLocation location, const QString &fallback) {
  QString path = QStandardPaths::writableLocation(location);
  if (path.isEmpty()) path = QDir::temp().filePath(fallback);
  QDir().mkpath(path);
  return QDir::cleanPath(path);
}

QString existingCandidate(const QStringList &candidates) {
  for (const auto &candidate : candidates) {
    if (!candidate.isEmpty() && QFileInfo::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
  }
  return {};
}

bool copyDirectoryContents(const QString &source, const QString &destination) {
  QDir sourceDir(source);
  if (!sourceDir.exists() || !QDir().mkpath(destination)) return false;
  QDirIterator iterator(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QString sourcePath = iterator.next();
    const QString relative = sourceDir.relativeFilePath(sourcePath);
    const QString destinationPath = QDir(destination).filePath(relative);
    if (iterator.fileInfo().isDir()) {
      if (!QDir().mkpath(destinationPath)) return false;
    } else {
      QDir().mkpath(QFileInfo(destinationPath).absolutePath());
      if (!QFileInfo::exists(destinationPath) && !QFile::copy(sourcePath, destinationPath)) return false;
      QFile::setPermissions(destinationPath, iterator.fileInfo().permissions());
    }
  }
  return true;
}

} // namespace

QString appConfigDir() { return ensurePath(QStandardPaths::AppConfigLocation, "CyberSnapper/config"); }
QString appDataDir() { return ensurePath(QStandardPaths::AppLocalDataLocation, "CyberSnapper/data"); }
QString cacheDir() { return ensurePath(QStandardPaths::CacheLocation, "CyberSnapper/cache"); }
QString runtimeDir() {
  const QString path = ensurePath(QStandardPaths::RuntimeLocation, "CyberSnapper/runtime");
#ifndef Q_OS_WIN
  QFile::setPermissions(path, QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
#endif
  return path;
}

QString defaultProjectRoot() {
  const QString overridden = qEnvironmentVariable("CYBERSNAPPER_DEFAULT_PROJECT");
  if (!overridden.isEmpty()) return QDir::cleanPath(QFileInfo(overridden).absoluteFilePath());
  QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (pictures.isEmpty()) pictures = QDir::homePath();
  return QDir(pictures).filePath("CyberSnapper/Quick Captures");
}

QString agentServerName() {
  const QString overridden = qEnvironmentVariable("CYBERSNAPPER_AGENT_SERVER");
  if (!overridden.isEmpty()) return QDir::cleanPath(overridden);
  const QByteArray identity = (QDir::homePath() + QCoreApplication::organizationDomain()).toUtf8();
  const QString suffix = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(12));
  const QString name = QStringLiteral("net.cyberbrand.CyberSnapper.Agent.v2.%1").arg(suffix);
#ifdef Q_OS_WIN
  return name;
#else
  // A filesystem socket is visible across process/network namespaces and can
  // be permissioned by the runtime directory. Qt's abstract Unix sockets are
  // namespace-local, which breaks sandboxed desktop launches.
  return QDir(runtimeDir()).filePath(name + ".sock");
#endif
}

QString agentExecutable() {
  const QString env = qEnvironmentVariable("CYBERSNAPPER_AGENT");
  const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
  const QString name = "cybersnapper-agent.exe";
#else
  const QString name = "cybersnapper-agent";
#endif
  const QString found = existingCandidate({env, QDir(appDir).filePath(name), QDir(appDir).filePath("../bin/" + name)});
  return found.isEmpty() ? name : found;
}

QString guiExecutable() {
  const QString env = qEnvironmentVariable("CYBERSNAPPER_GUI");
  const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
  const QString name = "CyberSnapper.exe";
#else
  const QString name = "CyberSnapper";
#endif
  const QString found = existingCandidate({env, QDir(appDir).filePath(name),
                                            QDir(appDir).filePath("../bin/" + name)});
  return found.isEmpty() ? name : found;
}

QString workerEntry() {
  const QString env = qEnvironmentVariable("CYBERSNAPPER_WORKER_ENTRY");
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString source = QStringLiteral(CYBERSNAPPER_SOURCE_ROOT "/worker/dist/main.cjs");
#ifdef Q_OS_MACOS
  const QString resources = QDir(appDir).filePath("../Resources/worker/main.cjs");
#else
  const QString resources = QDir(appDir).filePath("../share/cybersnapper/worker/main.cjs");
#endif
  return existingCandidate({env, source, resources, QDir(appDir).filePath("worker/main.cjs")});
}

QString nodeExecutable() {
  const QString env = qEnvironmentVariable("CYBERSNAPPER_NODE");
  const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
  const QString bundled = QDir(appDir).filePath("runtime/node.exe");
#elif defined(Q_OS_MACOS)
  const QString bundled = QDir(appDir).filePath("../Resources/runtime/node");
#else
  const QString bundled = QDir(appDir).filePath("../lib/cybersnapper/runtime/node");
#endif
  const QString found = existingCandidate({env, bundled});
  return found.isEmpty() ? QStringLiteral("node") : found;
}

QString browserCacheDir() {
  const QString overridden = qEnvironmentVariable("CYBERSNAPPER_BROWSER_CACHE");
  if (!overridden.isEmpty()) {
    QDir().mkpath(overridden);
    return QDir::cleanPath(overridden);
  }
  const QString cacheGeneration = QStringLiteral("v%1").arg(QStringLiteral(CYBERSNAPPER_VERSION).section('.', 0, 0));
  const QString writable = QDir(appDataDir()).filePath("browsers/" + cacheGeneration);
  QDir().mkpath(writable);
  const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
  const QString bundled = QDir(appDir).filePath("browsers");
#elif defined(Q_OS_MACOS)
  const QString bundled = QDir(appDir).filePath("../Resources/browsers");
#else
  const QString bundled = QDir(appDir).filePath("../share/cybersnapper/browsers");
#endif
  if (QDir(bundled).exists() && QDir(writable).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
    copyDirectoryContents(bundled, writable);
  }
  return QDir::cleanPath(writable);
}

bool ensureDirectory(const QString &path, QString *error) {
  if (QDir().mkpath(path)) return true;
  if (error) *error = QStringLiteral("Could not create directory: %1").arg(path);
  return false;
}

} // namespace CyberSnapper::Paths
