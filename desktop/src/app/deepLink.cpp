#include "deepLink.hpp"
#include "serverClient.hpp"
#include <QJsonDocument>
#include <QUrl>

namespace stencil::gui::deepLink {

  QString buildBrowserLaunchUrl(const QString& browserBase, const QJsonObject& payload) {
    QString base = browserBase.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    // toPercentEncoding leaves only unreserved chars (A-Za-z0-9-._~) bare — a strict
    // subset of what encodeURIComponent leaves, so decodeURIComponent reads it back.
    return base + QStringLiteral("#stencil=") +
           QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(json)));
  }

  // Drop the scheme from a normalized origin when it matches what normalizeBase
  // would infer for the bare host (mirrors browser deepLink.js compressOrigin).
  static QString compressOrigin(const QString& origin) {
    const QUrl u(origin);
    const QString defaultScheme =
        net::ServerClient::isLoopbackHost(u.host()) ? QStringLiteral("http")
                                                    : QStringLiteral("https");
    if (u.scheme() != defaultScheme) return origin;
    return u.port() != -1 ? QStringLiteral("%1:%2").arg(u.host()).arg(u.port())
                          : u.host();
  }

  QString encodeTelegramStartPayload(const QString& serverUrl, const QString& projectId) {
    const QString origin = net::ServerClient::normalizeBase(serverUrl);
    const QString plain = compressOrigin(origin) + QLatin1Char('|') + projectId;
    const QString payload = QStringLiteral("1") + QString::fromLatin1(
        plain.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    return payload.size() <= kTelegramStartLimit ? payload : QString();
  }

  QString buildTelegramLink(const QString& botUsername, const QString& payload) {
    return QStringLiteral("https://t.me/%1?start=%2").arg(botUsername, payload);
  }

}  // namespace stencil::gui::deepLink
