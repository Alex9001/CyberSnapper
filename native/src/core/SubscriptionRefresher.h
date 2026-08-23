#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QTimer;
class QUrl;

namespace CyberSnapper {

struct SubscriptionInfo;

// Downloads and refreshes curated community filter lists into the app-local
// cache. Runs only inside the agent process and never during a capture:
// captures read whatever snapshot was current when they were submitted.
//
// Safety rules mirror worker/src/network.ts: explicit HTTPS only, no embedded
// credentials, resolved hosts must be public, redirects are validated hop by
// hop, each transfer is capped in size and time, and a failed refresh keeps
// the last known good list.
class SubscriptionRefresher final : public QObject {
  Q_OBJECT
public:
  struct CacheMeta {
    QString digest;
    QString etag;
    QString lastModified;
    qint64 ruleCount = 0;
    QString fetchedAt;
    QString expiresAt;
  };

  explicit SubscriptionRefresher(QObject *parent = nullptr);
  ~SubscriptionRefresher() override;

  void start();
  void stop();

  // True while any transfer is in flight.
  bool busy() const;

  // Force a refresh of one subscription (empty id = all due entries).
  void refreshNow(const QString &subscriptionId);

signals:
  void refreshed(const QString &subscriptionId, bool ok, const QString &message);
  void warning(const QString &message);

private:
  void refreshOne(const QString &subscriptionId, const QUrl &url, int redirectBudget);
  void issueRequest(const QUrl &url, const SubscriptionInfo *info, const QString &subscriptionId,
                    int redirectBudget, std::function<void(const QString &)> finishFailure);
  void scheduleNextCheck();

  QNetworkAccessManager *m_network = nullptr;
  std::unique_ptr<QTimer> m_timer;
  QStringList m_pending;
};

// Reads the cache metadata sidecar for a subscription (rule count, freshness).
QJsonObject subscriptionCacheMeta(const QString &subscriptionId);

} // namespace CyberSnapper
