#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// "Open in…" link builders + Telegram start-payload codec — browser twin js/core/launch/deepLink.js, bot
// twin Infrastructure/Links/DeepLinkCodec.cs; shared golden vectors in the three surfaces' tests.
namespace stencil::gui::deepLink {

  inline constexpr int TELEGRAM_START_LIMIT = 64;

  // LAUNCH.payloadMaxChars (common/config/constants.json through the qrc): the 32 MiB the browser
  // receiver also caps inbound dataUrls at (LAUNCH_DATA_URL_MAX).
  inline qsizetype browserLaunchPayloadMax() {
    static const qsizetype cap = [] {
      const qsizetype fallback = 32 * 1024 * 1024;   // a build without the qrc
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return fallback;
      const double v = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("LAUNCH"))
                           .toObject().value(QLatin1String("payloadMaxChars")).toDouble(0);
      return v > 0 ? static_cast<qsizetype>(v) : fallback;
    }();
    return cap;
  }

  // "<browserBase>#stencil=<percent-encoded JSON>"; decodeURIComponent-compatible.
  QString buildBrowserLaunchUrl(const QString& browserBase, const QJsonObject& payload);

  // "1" + base64url("host[:port]|projectId"), padding stripped; the scheme is kept only when it is
  // NOT what normalizeBase infers. Empty when over the 64-char cap — callers fall back to the commands.
  QString encodeTelegramStartPayload(const QString& serverUrl, const QString& projectId);

  QString buildTelegramLink(const QString& botUsername, const QString& payload);

}  // namespace stencil::gui::deepLink
