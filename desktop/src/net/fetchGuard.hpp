#pragma once
// SSRF guard for UNTRUSTED http(s) fetches — port of cli/src/net.zig with its two-tier `strict`:
// internal ranges always blocked, loopback only when strict (a URL from scanned/shared content).
// The server-connect path is exempt — users name their own servers (net.zig:71).
#include <QByteArray>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <functional>

class QObject;

namespace stencil::net::fetchGuard {

  // ServerClient.cpp bounds its own requests with the same 20s.
  inline constexpr qint64 MAX_FETCH_BYTES = 64 * 1024 * 1024;
  inline constexpr int FETCH_TIMEOUT_MS = 20000;

  // Literal check only, including the alternate numeric IPv4 encodings a resolver accepts;
  // a DNS name is covered by resolvesToBlocked().
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
           int deadlineMs = FETCH_TIMEOUT_MS);

}  // namespace stencil::net::fetchGuard
