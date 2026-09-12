#include "fetchGuard.hpp"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QHostInfo>
#include <QNetworkReply>
#include <QStringList>
#include <QTimer>

namespace stencil::net::fetchGuard {

  namespace {

    bool isBlockedV4(const quint8* b, bool strict) {
      if (b[0] == 0) return true;                                 // 0.0.0.0/8 this-network
      if (b[0] == 10) return true;                                // 10.0.0.0/8 private
      if (b[0] == 100 && b[1] >= 64 && b[1] <= 127) return true;  // 100.64.0.0/10 CGNAT
      // 127.0.0.0/8 loopback: allowed for a user-named URL, blocked for scanned content.
      if (strict && b[0] == 127) return true;
      if (b[0] == 169 && b[1] == 254) return true;                // link-local (metadata)
      if (b[0] == 172 && b[1] >= 16 && b[1] <= 31) return true;   // 172.16.0.0/12 private
      if (b[0] == 192 && b[1] == 168) return true;                // 192.168.0.0/16 private
      if (b[0] == 192 && b[1] == 0 && (b[2] == 0 || b[2] == 2)) return true;  // /24, TEST-NET-1
      if (b[0] == 198 && (b[1] == 18 || b[1] == 19)) return true;  // 198.18.0.0/15 benchmarking
      if (b[0] == 198 && b[1] == 51 && b[2] == 100) return true;   // TEST-NET-2
      if (b[0] == 203 && b[1] == 0 && b[2] == 113) return true;    // TEST-NET-3
      if (b[0] >= 240) return true;                                // 240.0.0.0/4 + broadcast
      return false;
    }

    bool isBlockedV6(const quint8* b, bool strict) {
      // IPv4-mapped ::ffff:0:0/96 — classify the embedded IPv4.
      bool mapped = b[10] == 0xff && b[11] == 0xff;
      for (int i = 0; i < 10 && mapped; ++i) mapped = b[i] == 0;
      if (mapped) return isBlockedV4(b + 12, strict);
      bool allZero = true;
      for (int i = 0; i < 15; ++i) allZero = allZero && b[i] == 0;
      if (allZero && b[15] == 1) return strict;  // ::1 loopback, as isBlockedV4's 127/8
      if (allZero && b[15] == 0) return true;    // :: unspecified
      if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return true;  // fe80::/10 link-local
      if (b[0] == 0xfe && (b[1] & 0xc0) == 0xc0) return true;  // fec0::/10 site-local
      if ((b[0] & 0xfe) == 0xfc) return true;                  // fc00::/7 unique-local
      return false;
    }

    bool classify(const QHostAddress& addr, bool strict) {
      if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v = addr.toIPv4Address();
        const quint8 b[4] = {quint8(v >> 24), quint8(v >> 16), quint8(v >> 8), quint8(v)};
        return isBlockedV4(b, strict);
      }
      if (addr.protocol() == QAbstractSocket::IPv6Protocol)
        return isBlockedV6(addr.toIPv6Address().c, strict);
      return true;  // unparseable / any-address → refuse
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

    // inet_aton for 1–4 numeric parts: the encodings a resolver accepts but QHostAddress rejects.
    bool parseInetAtonV4(const QString& host, quint8* out) {
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
      out[0] = quint8((value >> 24) & 0xff);
      out[1] = quint8((value >> 16) & 0xff);
      out[2] = quint8((value >> 8) & 0xff);
      out[3] = quint8(value & 0xff);
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

  bool isBlockedHost(const QString& host, bool strict) {
    if (host.isEmpty()) return true;
    // QHostAddress and inet_aton agree today; a host is refused if EITHER reads internal.
    quint8 v4[4];
    if (parseInetAtonV4(host, v4) && isBlockedV4(v4, strict)) return true;
    QHostAddress addr;
    if (addr.setAddress(host)) return classify(addr, strict);
    // The literal `localhost` name (strict only — a name, so setAddress missed it).
    if (strict && host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) return true;
    return false;  // a real hostname: resolvesToBlocked() covers name→internal
  }

  bool isNumericHost(const QString& host) {
    QHostAddress addr;
    if (addr.setAddress(host)) return true;
    quint8 v4[4];
    return parseInetAtonV4(host, v4);
  }

  bool resolvesToBlocked(const QString& host, bool strict) {
    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError) return false;
    for (const QHostAddress& a : info.addresses())
      if (classify(a, strict)) return true;
    return false;
  }

  bool isWebScheme(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
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
    req.setTransferTimeout(FETCH_TIMEOUT_MS);
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
