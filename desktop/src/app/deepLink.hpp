#pragma once
#include <QJsonObject>
#include <QString>

// Cross-front-end "Open in…" link builders + the Telegram start-payload codec —
// the desktop counterpart of browser/js/core/deepLink.js (buildExternalLaunchUrl,
// encodeTelegramStartPayload). The bot carries the same codec in
// Application/Links/DeepLinkCodec.cs. Keep the three behaviorally identical; the
// shared golden vectors live in desktop/tests/deepLink.headless.cpp,
// browser/tests/deepLink.test.js and bot .../DeepLinkCodecTests.cs.
namespace stencil::gui::deepLink {

  // Telegram caps `?start=` payloads at 64 chars from the charset [A-Za-z0-9_-].
  inline constexpr int kTelegramStartLimit = 64;

  // "<browserBase>#stencil=<percent-encoded JSON>" — the browser app's external-launch
  // fragment (DrawingApp.applyExternalLaunch). The encoding is decodeURIComponent-
  // compatible; a trailing '/' on the base is dropped.
  QString buildBrowserLaunchUrl(const QString& browserBase, const QJsonObject& payload);

  // Encode (server origin, project id) into a t.me start payload: "1" (version
  // marker) + base64url("host[:port]|projectId"), padding stripped. The scheme is
  // kept only when it is NOT what normalizeBase would infer for the bare host
  // (https for remote, http for loopback) — the decoder re-normalizes, so the
  // default scheme round-trips from just host[:port]. Returns an empty string when
  // the payload would exceed the 64-char limit; callers must then fall back to
  // showing copyable `/connect <url>` + `/fetch <id>` commands.
  QString encodeTelegramStartPayload(const QString& serverUrl, const QString& projectId);

  QString buildTelegramLink(const QString& botUsername, const QString& payload);

}  // namespace stencil::gui::deepLink
