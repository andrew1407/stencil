#pragma once
#include <QJsonObject>
#include <QString>

// "Open in…" link builders + Telegram start-payload codec — browser twin js/core/deepLink.js, bot
// twin Application/Links/DeepLinkCodec.cs; shared golden vectors in the three surfaces' tests.
namespace stencil::gui::deepLink {

  inline constexpr int kTelegramStartLimit = 64;

  // The browser receiver caps inbound dataUrls at the same 32 MiB (LAUNCH_DATA_URL_MAX).
  inline constexpr qsizetype kBrowserLaunchPayloadMax = 32 * 1024 * 1024;

  // "<browserBase>#stencil=<percent-encoded JSON>"; decodeURIComponent-compatible.
  QString buildBrowserLaunchUrl(const QString& browserBase, const QJsonObject& payload);

  // "1" + base64url("host[:port]|projectId"), padding stripped; the scheme is kept only when it is
  // NOT what normalizeBase infers. Empty when over the 64-char cap — callers fall back to the commands.
  QString encodeTelegramStartPayload(const QString& serverUrl, const QString& projectId);

  QString buildTelegramLink(const QString& botUsername, const QString& payload);

}  // namespace stencil::gui::deepLink
