#include "fetchGuard.hpp"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QHostInfo>
#include <QNetworkReply>
#include <QStringList>
#include <QTimer>

namespace stencil::net::fetchGuard {

  namespace {

    bool classify(const QHostAddress& addr, bool strict) {
      return blockedRanges::blocked(addr, blockedRanges::Policy::FETCH, {!strict, false});
    }

    // A literal as its resolver reads it: brackets and an IPv6 zone ID dropped.
    QString literalOf(const QString& host) {
      QString bare = host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']')) ? host.mid(1, host.size() - 2) : host;
      const int zone = bare.indexOf(QLatin1Char('%'));
      if (zone >= 0 && bare.contains(QLatin1Char(':'))) bare.truncate(zone);
      return bare;
    }

    int hexVal(QChar c) {
      const char ch = c.toLatin1();
      if (ch >= '0' && ch <= '9') return ch - '0';
      if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
      if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
      return -1;
    }

    // One inet_aton component: `0x`-hex, leading-`0` octal, else decimal.
    bool parseAtonPart(const QString& s, quint64& out) {
      int base = 10;
      QString body = s;
      if (s.size() >= 2 && s[0] == QLatin1Char('0')
          && (s[1] == QLatin1Char('x') || s[1] == QLatin1Char('X'))) {
        base = 16;
        body = s.mid(2);
      } else if (s.size() >= 2 && s[0] == QLatin1Char('0')) {
        base = 8;
        body = s.mid(1);
      }
      if (body.isEmpty()) return false;
      for (const QChar c : body) {
        const int v = hexVal(c);
        if (v < 0 || v >= base) return false;
      }
      bool ok = false;
      out = body.toULongLong(&ok, base);
      return ok;
    }

    // inet_aton for 1–4 numeric parts, the encodings a resolver accepts; kept beside QHostAddress's own.
    bool parseInetAtonV4(const QString& host, quint32* out) {
      if (host.isEmpty() || hexVal(host[0]) < 0 || hexVal(host[0]) > 9) return false;
      const QStringList parts = host.split(QLatin1Char('.'));
      if (parts.size() > 4) return false;
      quint64 p[4] = {0, 0, 0, 0};
      for (int i = 0; i < parts.size(); ++i)
        if (!parseAtonPart(parts[i], p[i])) return false;
      const int n = parts.size();
      quint64 value = 0;
      for (int i = 0; i < n - 1; ++i) {
        if (p[i] > 0xff) return false;
        value |= p[i] << (24 - 8 * i);
      }
      if (p[n - 1] >= (1ULL << (8 * (5 - n)))) return false;
      value |= p[n - 1];
      *out = quint32(value);
      return true;
    }

    void capSize(QNetworkReply* reply) {
      QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                       [reply](qint64 got, qint64 total) {
                         if (got > MAX_FETCH_BYTES || total > MAX_FETCH_BYTES) reply->abort();
                       });
    }

    bool refusedRedirect(QNetworkReply* reply) {
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      return status >= 300 && status < 400;
    }

    void send(QObject* ctx, const QUrl& url, int deadlineMs,
              std::function<void(QByteArray, QString)> done) {
      auto* nam = new QNetworkAccessManager(ctx);
      QNetworkReply* reply = nam->get(request(url));
      capSize(reply);
      QTimer::singleShot(deadlineMs, reply, [reply] { reply->abort(); });
      QObject::connect(reply, &QNetworkReply::finished, nam,
                       [reply, nam, done = std::move(done)]() mutable {
                         QByteArray body;
                         QString error;
                         if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
                         else if (refusedRedirect(reply)) error = QStringLiteral("that URL redirects elsewhere");
                         else body = reply->readAll();
                         reply->deleteLater();
                         nam->deleteLater();
                         done(body, error);
                       });
    }

    QString refusal(const QString& host) {
      return QStringLiteral("refusing to fetch internal/blocked host '%1'").arg(host);
    }

  }  // namespace

  QHostAddress hostAddress(const QString& host) {
    const QString bare = literalOf(host);
    QHostAddress addr;
    if (addr.setAddress(bare)) return addr;
    quint32 v4 = 0;
    return parseInetAtonV4(bare, &v4) ? QHostAddress(v4) : QHostAddress();
  }

  bool refusesLiteral(const QString& host, blockedRanges::Policy policy, blockedRanges::Options options) {
    // QHostAddress and inet_aton agree today; a host is refused if EITHER reads it refused.
    const QString bare = literalOf(host);
    quint32 v4 = 0;
    if (parseInetAtonV4(bare, &v4) && blockedRanges::blocked(QHostAddress(v4), policy, options)) return true;
    QHostAddress addr;
    return addr.setAddress(bare) && blockedRanges::blocked(addr, policy, options);
  }

  bool isBlockedHost(const QString& host, bool strict) {
    if (host.isEmpty()) return true;
    if (refusesLiteral(host, blockedRanges::Policy::FETCH, {!strict, false})) return true;
    if (isNumericHost(host)) return false;
    // The literal `localhost` name (strict only — a name, so no parser read it).
    return strict && host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0;
  }

  bool isNumericHost(const QString& host) { return !hostAddress(host).isNull(); }

  bool resolvesToBlocked(const QString& host, bool strict) {
    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError) return false;
    for (const QHostAddress& a : info.addresses())
      if (classify(a, strict)) return true;
    return false;
  }

  QString blockedReason(const QUrl& url, bool strict) {
    if (!isWebScheme(url)) return QStringLiteral("only http(s) URLs can be fetched");
    const QString host = url.host();
    if (host.isEmpty()) return QStringLiteral("that URL has no host");
    return isBlockedHost(host, strict) ? refusal(host) : QString();
  }

  void checkAsync(QObject* ctx, const QUrl& url, bool strict,
                  std::function<void(QString)> done) {
    const QString reason = blockedReason(url, strict);
    const QString host = url.host();
    if (!reason.isEmpty() || isNumericHost(host)) {
      // Deliver on the event loop, so a refusal reaches callers the same way a fetch does.
      QTimer::singleShot(0, ctx, [reason, done = std::move(done)] { done(reason); });
      return;
    }
    QHostInfo::lookupHost(host, ctx,
                          [strict, host, done = std::move(done)](const QHostInfo& info) {
                            bool blocked = false;
                            if (info.error() == QHostInfo::NoError)
                              for (const QHostAddress& a : info.addresses())
                                blocked = blocked || classify(a, strict);
                            done(blocked ? refusal(host) : QString());
                          });
  }

  QNetworkRequest request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setTransferTimeout(fetchTimeoutMs());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    return req;
  }

  void get(QObject* ctx, const QUrl& url, bool strict,
           std::function<void(QByteArray, QString)> done, int deadlineMs) {
    checkAsync(ctx, url, strict,
               [ctx, url, deadlineMs, done = std::move(done)](const QString& why) mutable {
                 if (why.isEmpty()) send(ctx, url, deadlineMs, std::move(done));
                 else done(QByteArray(), why);
               });
  }

}  // namespace stencil::net::fetchGuard
