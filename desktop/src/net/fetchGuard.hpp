#pragma once
// SSRF guard for UNTRUSTED http(s) fetches — the desktop port of cli/src/net.zig
// (isBlockedFetchHost / isBlockedV4 / isBlockedV6 / parseInetAtonV4 /
// hostResolvesToBlocked), with the same two-tier `strict` design: the internal ranges
// are always blocked, loopback only when `strict` (a URL taken from scanned or shared
// content rather than one the user typed). The server-connect path is deliberately
// exempt — users name their own servers (net.zig:71).
#include <QByteArray>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <functional>

class QObject;

namespace stencil::net::fetchGuard {

  // Hard cap on the bytes one fetch may read, and the house transfer timeout
  // (serverClient.cpp bounds its own requests with the same 20s).
  inline constexpr qint64 kMaxFetchBytes = 64 * 1024 * 1024;
  inline constexpr int kFetchTimeoutMs = 20000;

  // True when `host` (bare: no port, no IPv6 brackets) names a private / link-local /
  // cloud-metadata / reserved target, including the alternate numeric IPv4 encodings
  // (decimal / hex / octal / short-dotted) a resolver accepts. Literal check only —
  // a DNS name is covered by resolvesToBlocked().
  bool isBlockedHost(const QString& host, bool strict);

  // True when `host` is an IP form rather than a DNS name (so it needs no lookup).
  bool isNumericHost(const QString& host);

  // Resolve `host` and report whether ANY answer is internal. Blocking; prefer
  // checkAsync on the GUI thread. Honest limitation, as in net.zig: a record flipped
  // between this lookup and Qt's own connect (active DNS rebinding) still gets through;
  // the redirect refusal below only closes the 30x variant.
  bool resolvesToBlocked(const QString& host, bool strict);

  // True when `url` carries a web scheme — the only ones that may be fetched, or handed
  // to the OS URL handler (a typed file:/smb: URL must never be launched).
  bool isWebScheme(const QUrl& url);

  // "" when `url` may be fetched, else a short reason. Literal check only.
  QString blockedReason(const QUrl& url, bool strict);

  // blockedReason() plus, for a DNS name, the resolution check. `done` always fires
  // asynchronously on `ctx`'s thread, and never once `ctx` is gone.
  void checkAsync(QObject* ctx, const QUrl& url, bool strict,
                  std::function<void(QString reason)> done);

  // The house limits for an untrusted request: the transfer timeout and NO redirects
  // (a public first hop must not 30x-bounce to an internal host).
  QNetworkRequest request(const QUrl& url);

  // One guarded GET — the check above, then a request under those limits with the body
  // capped at kMaxFetchBytes and the whole exchange bounded by `deadlineMs`. `done` fires
  // once on `ctx`'s thread: the body, or an empty body and the reason it did not arrive
  // (a refused host, a redirect, the cap, or the transport's own error).
  void get(QObject* ctx, const QUrl& url, bool strict,
           std::function<void(QByteArray body, QString error)> done,
           int deadlineMs = kFetchTimeoutMs);

}  // namespace stencil::net::fetchGuard
