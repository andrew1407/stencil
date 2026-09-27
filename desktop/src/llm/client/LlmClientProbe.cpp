#include "LlmClient.hpp"
#include "llmClientShared.hpp"

#include "ServerClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace stencil::llm {

  namespace {
    typedef QList<QPair<QByteArray, QByteArray>> Headers;

    QStringList namesIn(const QJsonObject& o, const char* list, const char* key) {
      QStringList out;
      for (const QJsonValue& v : o.value(QLatin1String(list)).toArray()) {
        const QString name = v.toObject().value(QLatin1String(key)).toString();
        if (!name.isEmpty()) out << name;
      }
      return out;
    }

    // How a wire's GETs authorize: none, the optional Bearer key, the Stencil session bearer on the
    // server URL, or the §6.5 session-key headers.
    enum class Auth { NONE, BEARER_KEY, SESSION, SESSION_KEY };

    // One strategy per providers.json `wire` (llm-providers.md §6.4): what its model list and probe answer.
    struct Wire {
      const char* name;
      Auth auth;
      QStringList (*models)(const QJsonObject&);
      void (*probe)(const QJsonObject&, LlmProbeResult&);
    };

    void firstModelId(const QJsonObject& o, LlmProbeResult& r) {
      r.ok = true;
      r.detail = o.value("data").toArray().at(0).toObject().value("id").toString();
    }

    const Wire WIRES[] = {
        {"ollama", Auth::NONE, [](const QJsonObject& o) { return namesIn(o, "models", "name"); },
         [](const QJsonObject& o, LlmProbeResult& r) {
           const QString version = o.value("version").toString();
           r.ok = true;
           r.detail = version.isEmpty() ? QString() : QStringLiteral("v") + version;
         }},
        {"openai", Auth::BEARER_KEY, [](const QJsonObject& o) { return namesIn(o, "data", "id"); },
         firstModelId},
        {"anthropic", Auth::SESSION_KEY, [](const QJsonObject& o) { return namesIn(o, "data", "id"); },
         firstModelId},
        {"server", Auth::SESSION,
         [](const QJsonObject& o) {
           const QString model = o.value("model").toString();
           return model.isEmpty() ? QStringList() : QStringList{model};
         },
         [](const QJsonObject& o, LlmProbeResult& r) {
           r.model = o.value("model").toString();
           r.ok = o.value("enabled").toBool(false);
           r.detail = r.ok ? r.model : QStringLiteral("LLM disabled on this server");
         }},
    };

    // The provider's providers.json row and its wire, or null for an unknown provider.
    const Wire* wireOf(const QJsonObject& info) {
      const QString name = info.value("wire").toString();
      for (const Wire& w : WIRES)
        if (name == QLatin1String(w.name)) return &w;
      return nullptr;
    }

    // Where a models or probe GET goes and with what auth; `why` is set when nothing may be sent.
    QUrl targetOf(const LlmSettings& cfg, const Wire& w, const QString& path,
                  const std::function<QString(const QString&)>& tokenResolver, Headers* headers, QString* why) {
      if (w.auth == Auth::SESSION_KEY) {
        *why = cfg.apiKey.isEmpty() ? QString::fromUtf8(NO_SESSION_KEY_TEXT) : plainHttpRefusal(trimSlash(cfg.baseUrl));
        if (!why->isEmpty()) return QUrl();
        *headers = anthropicHeaders(cfg.apiKey);
      }
      if (w.auth != Auth::SESSION) {
        if (w.auth == Auth::BEARER_KEY && !cfg.apiKey.isEmpty())
          headers->append({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + cfg.apiKey.toUtf8()});
        return QUrl(trimSlash(cfg.baseUrl) + path);
      }
      const QString base = net::ServerClient::normalizeBase(cfg.serverUrl);
      if (base.isEmpty()) {
        *why = QStringLiteral("no collaboration server configured");
        return QUrl();
      }
      const QString token = tokenResolver(cfg.serverUrl);
      if (token.isEmpty()) {
        *why = QStringLiteral("not connected (no token for %1)").arg(base);
        return QUrl();
      }
      headers->append({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8()});
      return QUrl(base + path);
    }
  }  // namespace

  // reachability probe (chat-dock status dot)

  void LlmClient::probe(const LlmSettings& cfg, std::function<void(LlmProbeResult)> done) {
    const auto fail = [&done](const QString& detail) {
      LlmProbeResult r;
      r.detail = detail;
      done(r);
    };
    // "none" probes nothing — the assistant is off by configuration.
    if (cfg.provider == QLatin1String("none")) return fail(QStringLiteral("assistant turned off"));
    const QJsonObject info = providerCanonEntry(cfg.provider);
    const Wire* wire = wireOf(info);
    if (!wire) return fail(QStringLiteral("unknown provider \"%1\"").arg(cfg.provider));
    Headers headers;
    QString why;
    const QUrl url = targetOf(cfg, *wire, info.value("probePath").toString(), tokenResolver, &headers, &why);
    if (!why.isEmpty()) return fail(why);
    transport->getJson(url, headers, probeHandler(std::move(done), wire->probe));
  }

  // model suggestions (settings UI; browser listModels parity)

  void LlmClient::listModels(const LlmSettings& cfg, std::function<void(QStringList)> done) {
    const QJsonObject info = providerCanonEntry(cfg.provider);
    const Wire* wire = cfg.provider == QLatin1String("none") ? nullptr : wireOf(info);
    Headers headers;
    QString why;
    const QUrl url = wire ? targetOf(cfg, *wire, info.value("modelsPath").toString(), tokenResolver, &headers, &why) : QUrl();
    if (!wire || !why.isEmpty()) {  // assistant off, an unknown provider, or no auth: nothing to list
      done({});
      return;
    }
    // Any non-2xx / transport failure ⇒ empty list (suggestions are optional).
    transport->getJson(url, headers, [done = std::move(done), pick = wire->models](int status, QByteArray body, QString) {
      done(net::isOkStatus(status) ? pick(QJsonDocument::fromJson(body).object()) : QStringList());
    });
  }
}  // namespace stencil::llm
