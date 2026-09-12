#include "llmClient.hpp"
#include "llmClientShared.hpp"

#include "connectionStore.hpp"
#include "opRegistry.hpp"
#include "serverClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace stencil::llm {

  // reachability probe (chat-dock status dot)

  void LlmClient::probe(const LlmSettings& cfg, std::function<void(LlmProbeResult)> done) {
    const auto fail = [&done](const QString& detail) {
      LlmProbeResult r;
      r.detail = detail;
      done(r);
    };
    // "none" probes nothing — the assistant is off by configuration.
    if (cfg.provider == QLatin1String("none"))
      return fail(QStringLiteral("assistant turned off"));
    if (cfg.provider == QLatin1String("stencil-server")) {
      const QString base = net::ServerClient::normalizeBase(cfg.serverUrl);
      if (base.isEmpty()) return fail(QStringLiteral("no collaboration server configured"));
      const QString token = tokenResolver_(cfg.serverUrl);
      if (token.isEmpty())
        return fail(QStringLiteral("not connected (no token for %1)").arg(base));
      transport_->getJson(
          QUrl(base + QStringLiteral("/llm/info")),
          {{QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + token.toUtf8()}},
          probeHandler(std::move(done), [](const QJsonObject& o, LlmProbeResult& r) {
            r.model = o.value("model").toString();
            if (o.value("enabled").toBool(false)) r.ok = true;
            else r.detail = QStringLiteral("LLM disabled on the server");
          }));
      return;
    }
    const bool openai = cfg.provider == QLatin1String("openai-compat");
    QList<QPair<QByteArray, QByteArray>> headers;
    if (openai && !cfg.apiKey.isEmpty())
      headers.append({QByteArrayLiteral("Authorization"),
                      QByteArrayLiteral("Bearer ") + cfg.apiKey.toUtf8()});
    const QUrl url(trimSlash(cfg.baseUrl) +
                   (openai ? QStringLiteral("/models") : QStringLiteral("/api/version")));
    transport_->getJson(
        url, headers,
        probeHandler(std::move(done), [](const QJsonObject& o, LlmProbeResult& r) {
          r.ok = true;
          // Ollama's /api/version reports a version string worth surfacing.
          r.detail = o.value("version").toString();
        }));
  }

  // model suggestions (settings UI; browser listModels parity)

  void LlmClient::listModels(const LlmSettings& cfg, std::function<void(QStringList)> done) {
    using Pick = QStringList (*)(const QJsonObject&);
    // Any non-2xx / transport failure ⇒ empty list (suggestions are optional).
    const auto handler = [](std::function<void(QStringList)> done, Pick pick) {
      return [done = std::move(done), pick](int status, QByteArray body, QString) mutable {
        QStringList out;
        if (status >= 200 && status < 300)
          out = pick(QJsonDocument::fromJson(body).object());
        done(out);
      };
    };
    if (cfg.provider == QLatin1String("none")) {  // assistant off — nothing to list
      done({});
      return;
    }
    if (cfg.provider == QLatin1String("stencil-server")) {
      const QString base = net::ServerClient::normalizeBase(cfg.serverUrl);
      const QString token = base.isEmpty() ? QString() : tokenResolver_(cfg.serverUrl);
      if (token.isEmpty()) {
        done({});
        return;
      }
      transport_->getJson(
          QUrl(base + QStringLiteral("/llm/info")),
          {{QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + token.toUtf8()}},
          handler(std::move(done), [](const QJsonObject& o) {
            const QString model = o.value("model").toString();
            return model.isEmpty() ? QStringList() : QStringList{model};
          }));
      return;
    }
    const bool openai = cfg.provider == QLatin1String("openai-compat");
    QList<QPair<QByteArray, QByteArray>> headers;
    if (openai && !cfg.apiKey.isEmpty())
      headers.append({QByteArrayLiteral("Authorization"),
                      QByteArrayLiteral("Bearer ") + cfg.apiKey.toUtf8()});
    const QUrl url(trimSlash(cfg.baseUrl) +
                   (openai ? QStringLiteral("/models") : QStringLiteral("/api/tags")));
    transport_->getJson(
        url, headers,
        handler(std::move(done),
                openai ? Pick([](const QJsonObject& o) {
                  QStringList out;
                  for (const QJsonValue& v : o.value("data").toArray()) {
                    const QString id = v.toObject().value("id").toString();
                    if (!id.isEmpty()) out << id;
                  }
                  return out;
                })
                       : Pick([](const QJsonObject& o) {
                  QStringList out;
                  for (const QJsonValue& v : o.value("models").toArray()) {
                    const QString name = v.toObject().value("name").toString();
                    if (!name.isEmpty()) out << name;
                  }
                  return out;
                })));
  }
}  // namespace stencil::llm

