#include "deepLink.hpp"
#include "ServerClient.hpp"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

namespace stencil::gui::deepLink {


  QString buildBrowserLaunchUrl(const QString& browserBase, const QJsonObject& payload) {
    QString base = browserBase.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    // Over-limit payload → empty string (the encodeTelegramStartPayload overflow style).
    if (json.size() > browserLaunchPayloadMax()) return QString();
    // toPercentEncoding leaves only unreserved chars (A-Za-z0-9-._~) bare — a strict
    // subset of what encodeURIComponent leaves, so decodeURIComponent reads it back.
    return base + QStringLiteral("#stencil=") +
           QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(json)));
  }

  namespace {
    // Drop the scheme from a normalized origin when it matches what normalizeBase
    // would infer for the bare host (mirrors browser deepLink.js compressOrigin).
    QString compressOrigin(const QString& origin) {
      const QUrl u(origin);
      const QString defaultScheme =
          net::ServerClient::isLoopbackHost(u.host()) ? QStringLiteral("http")
                                                      : QStringLiteral("https");
      if (u.scheme() != defaultScheme) return origin;
      return u.port() != -1 ? QStringLiteral("%1:%2").arg(u.host()).arg(u.port())
                            : u.host();
    }
  }  // namespace

  QString encodeTelegramStartPayload(const QString& serverUrl, const QString& projectId) {
    const QString origin = net::ServerClient::normalizeBase(serverUrl);
    const QString plain = compressOrigin(origin) + QLatin1Char('|') + projectId;
    const QString payload = QStringLiteral("1") + QString::fromLatin1(
        plain.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    return payload.size() <= TELEGRAM_START_LIMIT ? payload : QString();
  }

  QString buildTelegramLink(const QString& botUsername, const QString& payload) {
    return QStringLiteral("https://t.me/%1?start=%2").arg(botUsername, payload);
  }

  namespace {
    // application/x-www-form-urlencoded as URLSearchParams writes it: `*` bare, `~` escaped, a space `+`.
    QString formValue(const QString& v) {
      return QString::fromLatin1(QUrl::toPercentEncoding(v, "*", "~")).replace(QLatin1String("%20"), QLatin1String("+"));
    }
    // The browser's own test (core/launch/shareLinks.js): a scheme, then at least one character, no space.
    bool isWebUrl(const QString& s) {
      static const QRegularExpression web(QStringLiteral("^https?://\\S+$"), QRegularExpression::CaseInsensitiveOption);
      return web.match(s).hasMatch();
    }
  }  // namespace

  std::vector<ShareLink> projectShareLinks(const QString& name, const QString& source,
                                           const QString& serverUrl, const QString& remoteId) {
    const QString open = QStringLiteral("stencil://open?");
    const QString named = name.trimmed(), from = source.trimmed();
    std::vector<ShareLink> out;
    if (!named.isEmpty()) out.push_back({ShareLinkKind::NAME, open + QStringLiteral("project=") + formValue(named)});
    if (isWebUrl(from)) out.push_back({ShareLinkKind::SOURCE, open + QStringLiteral("source=") + formValue(from)});
    if (!serverUrl.isEmpty() && !remoteId.isEmpty())
      out.push_back({ShareLinkKind::SERVER, open + QStringLiteral("server=") + formValue(serverUrl) + QStringLiteral("&id=") + formValue(remoteId)});
    return out;
  }

}  // namespace stencil::gui::deepLink
