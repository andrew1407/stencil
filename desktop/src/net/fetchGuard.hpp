#pragma once
// SSRF guard for UNTRUSTED http(s) fetches — port of cli/src/net.zig with its two-tier `strict`:
// the blockedRanges `fetch` policy, loopback allowed only when not strict (a URL the user typed).
// A server the user names is judged by `serverTarget` instead (ServerClient::isRefusedTarget).
#include "blockedRanges.hpp"

#include <QByteArray>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <functional>

class QObject;

namespace stencil::net::fetchGuard {

  inline constexpr qint64 MAX_FETCH_BYTES = 64 * 1024 * 1024;
  // A build without the qrc; tests/net/fetchGuard holds it equal to the table.
  inline constexpr int FETCH_TIMEOUT_FALLBACK_MS = 30000;

  // ms, constants.json NETWORK.fetchTimeoutMs (the browser's bound too), read once. ServerClient
  // and the plan's awaits are held to the same bound.
  inline int fetchTimeoutMs() {
    static const int ms = [] {
      QFile f(QStringLiteral(":/config/constants.json"));
      const int v = f.open(QIODevice::ReadOnly)
          ? QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("NETWORK"))
                .toObject().value(QStringLiteral("fetchTimeoutMs")).toInt()
          : 0;
      return v > 0 ? v : FETCH_TIMEOUT_FALLBACK_MS;
    }();
    return ms;
  }

  // The address a literal host names — brackets and a zone ID dropped, the inet_aton spellings
  // read — or a null address for a name.
  QHostAddress hostAddress(const QString& host);

  // A literal host under a policy: refused when either IPv4 reading, or QHostAddress's, is refused.
  bool refusesLiteral(const QString& host, blockedRanges::Policy policy, blockedRanges::Options options);

  // Literal check only (the `fetch` policy, allowLoopback = !strict); a DNS name is covered by
  // resolvesToBlocked().
  bool isBlockedHost(const QString& host, bool strict);

  bool isNumericHost(const QString& host);

  // Blocking; prefer checkAsync on the GUI thread. As in net.zig, a record flipped between this
  // lookup and Qt's own connect (DNS rebinding) still gets through; only the 30x variant is closed.
  bool resolvesToBlocked(const QString& host, bool strict);

  // A typed file:/smb: URL must never be fetched or handed to the OS URL handler.
  bool isWebScheme(const QUrl& url);

  QString blockedReason(const QUrl& url, bool strict);

  // `done` always fires asynchronously on `ctx`'s thread, and never once `ctx` is gone.
  void checkAsync(QObject* ctx, const QUrl& url, bool strict,
                  std::function<void(QString reason)> done);

  // NO redirects: a public first hop must not 30x-bounce to an internal host.
  QNetworkRequest request(const QUrl& url);

  // Body capped at MAX_FETCH_BYTES, the exchange bounded by `deadlineMs`; `done` fires once on
  // `ctx`'s thread with the body, or an empty body and the reason.
  void get(QObject* ctx, const QUrl& url, bool strict,
           std::function<void(QByteArray body, QString error)> done,
           int deadlineMs = fetchTimeoutMs());

}  // namespace stencil::net::fetchGuard
