#include "core/SubscriptionRefresher.h"

#include "core/ContentRulesets.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <functional>

namespace CyberSnapper {

namespace {

constexpr int kMaximumListBytes = 10 * 1024 * 1024;
constexpr qint64 kTransferTimeoutMs = 30000;
constexpr int kRefreshIntervalHours = 5 * 24; // Fallback expiry for lists.
constexpr int kMaximumRedirects = 5;

QString metaPath(const QString &subscriptionId) {
  return QDir(ContentRulesets::cacheDirectory()).filePath(subscriptionId + QStringLiteral(".json"));
}

QString listPath(const QString &subscriptionId) {
  return QDir(ContentRulesets::cacheDirectory()).filePath(subscriptionId + QStringLiteral(".txt"));
}

qint64 countRules(const QByteArray &bytes) {
  qint64 rules = 0;
  for (const QByteArray &line : bytes.split('\n')) {
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('!') || trimmed.startsWith('[')) continue;
    ++rules;
  }
  return rules;
}

SubscriptionRefresher::CacheMeta readMeta(const QString &subscriptionId) {
  SubscriptionRefresher::CacheMeta meta;
  QFile file(metaPath(subscriptionId));
  if (!file.open(QIODevice::ReadOnly)) return meta;
  const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
  meta.digest = object.value("digest").toString();
  meta.etag = object.value("etag").toString();
  meta.lastModified = object.value("lastModified").toString();
  meta.ruleCount = object.value("ruleCount").toInt();
  meta.fetchedAt = object.value("fetchedAt").toString();
  meta.expiresAt = object.value("expiresAt").toString();
  return meta;
}

void writeMeta(const QString &subscriptionId, const SubscriptionRefresher::CacheMeta &meta) {
  QSaveFile file(metaPath(subscriptionId));
  if (!file.open(QIODevice::WriteOnly)) return;
  const QJsonObject object{{"digest", meta.digest}, {"etag", meta.etag},
      {"lastModified", meta.lastModified}, {"ruleCount", double(meta.ruleCount)},
      {"fetchedAt", meta.fetchedAt}, {"expiresAt", meta.expiresAt}};
  file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  file.commit();
}

// The catalog only ever hands out fixed HTTPS URLs; this is defense in depth
// against a future catalog entry that is not safe to fetch.
bool sourceUrlIsAllowed(const QUrl &url, QString *reason) {
  if (url.scheme() != QLatin1String("https")) {
    if (reason) *reason = QStringLiteral("Subscription sources must use HTTPS");
    return false;
  }
  if (!url.userName().isEmpty() || !url.password().isEmpty()) {
    if (reason) *reason = QStringLiteral("Subscription sources must not carry credentials");
    return false;
  }
  if (url.host().isEmpty()) {
    if (reason) *reason = QStringLiteral("Subscription source has no host");
    return false;
  }
  return true;
}

} // namespace

SubscriptionRefresher::SubscriptionRefresher(QObject *parent) : QObject(parent) {}

SubscriptionRefresher::~SubscriptionRefresher() = default;

void SubscriptionRefresher::start() {
  if (!m_network) m_network = new QNetworkAccessManager(this);
  m_network->setTransferTimeout(kTransferTimeoutMs);
  scheduleNextCheck();
  // Populate or renew anything due shortly after startup.
  QTimer::singleShot(3000, this, [this] { refreshNow(QString{}); });
}

void SubscriptionRefresher::stop() { m_timer.reset(); }

bool SubscriptionRefresher::busy() const { return !m_pending.isEmpty(); }

void SubscriptionRefresher::scheduleNextCheck() {
  if (!m_timer) {
    m_timer = std::make_unique<QTimer>(this);
    m_timer->setSingleShot(true);
    connect(m_timer.get(), &QTimer::timeout, this, [this] { refreshNow(QString{}); });
  }
  m_timer->start(60 * 60 * 1000); // Hourly sweep; per-list expiry decides.
}

void SubscriptionRefresher::refreshNow(const QString &subscriptionId) {
  QStringList ids;
  if (!subscriptionId.isEmpty()) {
    bool known = false;
    for (const auto &info : subscriptionCatalog()) known = known || info.id == subscriptionId;
    if (!known) {
      emit warning(QStringLiteral("Unknown subscription '%1'").arg(subscriptionId));
      return;
    }
    ids.append(subscriptionId);
  } else {
    // A list whose metadata is missing or expired is due for a refresh.
    for (const auto &info : subscriptionCatalog()) {
      const QDateTime expires =
          QDateTime::fromString(readMeta(info.id).expiresAt, Qt::ISODate);
      if (!expires.isValid() || expires <= QDateTime::currentDateTimeUtc()) ids.append(info.id);
    }
  }
  for (const QString &id : ids) {
    if (m_pending.contains(id)) continue;
    m_pending.append(id);
    const auto &subscriptions = subscriptionCatalog();
    for (const auto &info : subscriptions) {
      if (info.id == id) {
        refreshOne(id, QUrl(info.sourceUrl), kMaximumRedirects);
        break;
      }
    }
  }
}

void SubscriptionRefresher::refreshOne(const QString &subscriptionId, const QUrl &url,
                                        int redirectBudget) {
  const SubscriptionInfo *info = nullptr;
  for (const auto &candidate : subscriptionCatalog()) {
    if (candidate.id == subscriptionId) info = &candidate;
  }
  if (!info) {
    m_pending.removeAll(subscriptionId);
    return;
  }
  const auto finishFailure = [this, info, subscriptionId](const QString &reason) {
    m_pending.removeAll(subscriptionId);
    emit refreshed(subscriptionId, false, reason);
    emit warning(QStringLiteral("Refresh of %1 stopped: %2").arg(info->name, reason));
  };

  QString reason;
  if (!sourceUrlIsAllowed(url, &reason)) return finishFailure(reason);

  // Asynchronous DNS validation keeps the agent thread responsive. Resolved
  // addresses must be public: this mirrors the capture boundary's rejection of
  // loopback, private, link-local, and multicast destinations.
  QHostInfo::lookupHost(url.host(), this,
      [this, url, info, subscriptionId, redirectBudget,
       finishFailure](const QHostInfo &lookup) {
        if (lookup.error() != QHostInfo::NoError || lookup.addresses().isEmpty()) {
          return finishFailure(QStringLiteral("Could not resolve %1").arg(url.host()));
        }
        for (const QHostAddress &address : lookup.addresses()) {
          // isGlobal() excludes loopback, link-local, unique-local/private,
          // multicast, broadcast, and unspecified addresses.
          if (!address.isGlobal()) {
            return finishFailure(QStringLiteral("%1 is not a public address")
                                     .arg(address.toString()));
          }
        }
        issueRequest(url, info, subscriptionId, redirectBudget, std::move(finishFailure));
      });
}

void SubscriptionRefresher::issueRequest(const QUrl &url, const SubscriptionInfo *info,
                                         const QString &subscriptionId, int redirectBudget,
                                         std::function<void(const QString &)> finishFailure) {
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setRawHeader(
      "User-Agent",
      QStringLiteral("CyberSnapper/%1").arg(QStringLiteral(CYBERSNAPPER_VERSION)).toUtf8());
  if (url == QUrl(info->sourceUrl)) {
    const CacheMeta conditional = readMeta(subscriptionId);
    if (!conditional.etag.isEmpty()) {
      request.setRawHeader("If-None-Match", conditional.etag.toUtf8());
    }
    if (!conditional.lastModified.isEmpty()) {
      request.setRawHeader("If-Modified-Since", conditional.lastModified.toUtf8());
    }
  }

  QNetworkReply *reply = m_network->get(request);
  const auto stopOversizedTransfer = [reply] {
    const qint64 declared = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    if (declared > kMaximumListBytes || reply->bytesAvailable() > kMaximumListBytes) {
      reply->setProperty("cybersnapperListTooLarge", true);
      reply->abort();
    }
  };
  connect(reply, &QNetworkReply::metaDataChanged, this, stopOversizedTransfer);
  connect(reply, &QIODevice::readyRead, this, stopOversizedTransfer);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, info, subscriptionId, redirectBudget, finishFailure] {
            reply->deleteLater();
            m_pending.removeAll(subscriptionId);
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (reply->property("cybersnapperListTooLarge").toBool()) {
              emit refreshed(subscriptionId, false, QStringLiteral("List exceeds 10 MiB"));
              emit warning(QStringLiteral("%1 exceeded the 10 MiB limit and was not updated")
                               .arg(info->name));
              return;
            }

            if (status == 304) {
              CacheMeta meta = readMeta(subscriptionId);
              meta.expiresAt = QDateTime::currentDateTimeUtc()
                                   .addSecs(kRefreshIntervalHours * 3600)
                                   .toString(Qt::ISODate);
              writeMeta(subscriptionId, meta);
              emit refreshed(subscriptionId, true, QStringLiteral("unchanged"));
              return;
            }

            if (status == 301 || status == 302 || status == 303 || status == 307
                || status == 308) {
              if (redirectBudget <= 0) return finishFailure(QStringLiteral("Too many redirects"));
              const QUrl target = reply->url().resolved(
                  reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
              QString redirectReason;
              if (!sourceUrlIsAllowed(target, &redirectReason)) {
                return finishFailure(redirectReason);
              }
              // Re-run the full validation chain (DNS included) on the target.
              m_pending.append(subscriptionId);
              refreshOne(subscriptionId, target, redirectBudget - 1);
              return;
            }

            if (reply->error() != QNetworkReply::NoError || status != 200) {
              // Keep the last known good list; captures fall back gracefully.
              emit refreshed(subscriptionId, false, reply->errorString());
              emit warning(QStringLiteral("Could not refresh %1: %2")
                               .arg(info->name, reply->errorString()));
              return;
            }

            const QByteArray body = reply->readAll();
            if (body.size() > kMaximumListBytes) {
              emit refreshed(subscriptionId, false, QStringLiteral("List exceeds 10 MiB"));
              emit warning(QStringLiteral("%1 exceeded the 10 MiB limit and was not updated")
                               .arg(info->name));
              return;
            }
            if (body.trimmed().isEmpty()) {
              emit refreshed(subscriptionId, false, QStringLiteral("Empty list body"));
              emit warning(
                  QStringLiteral("%1 returned an empty list; keeping previous version")
                      .arg(info->name));
              return;
            }

            // Atomic replace: never leave a truncated list in the cache.
            QSaveFile save(listPath(subscriptionId));
            if (!save.open(QIODevice::WriteOnly)) return finishFailure(save.errorString());
            save.write(body);
            if (!save.commit()) return finishFailure(save.errorString());

            CacheMeta meta;
            meta.digest = QString::fromLatin1(
                QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex());
            meta.ruleCount = countRules(body);
            meta.etag = QString::fromLatin1(reply->rawHeader(QByteArrayLiteral("ETag")));
            meta.lastModified =
                QString::fromLatin1(reply->rawHeader(QByteArrayLiteral("Last-Modified")));
            const QDateTime now = QDateTime::currentDateTimeUtc();
            meta.fetchedAt = now.toString(Qt::ISODate);
            meta.expiresAt = now.addSecs(kRefreshIntervalHours * 3600).toString(Qt::ISODate);
            writeMeta(subscriptionId, meta);
            emit refreshed(subscriptionId, true, QStringLiteral("%1 rule(s)").arg(meta.ruleCount));
          });
}

QJsonObject subscriptionCacheMeta(const QString &subscriptionId) {
  const SubscriptionRefresher::CacheMeta meta = readMeta(subscriptionId);
  const QFileInfo cached(listPath(subscriptionId));
  return QJsonObject{{"downloaded", cached.exists()},
                     {"cachedBytes", cached.exists() ? qint64(cached.size()) : qint64(0)},
                     {"digest", meta.digest},
                     {"ruleCount", double(meta.ruleCount)},
                     {"fetchedAt", meta.fetchedAt},
                     {"expiresAt", meta.expiresAt}};
}

} // namespace CyberSnapper
