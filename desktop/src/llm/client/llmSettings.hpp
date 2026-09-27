#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

// Q_INIT_RESOURCE must sit outside any namespace.
inline void stencilLlmEnsureAppResources() { Q_INIT_RESOURCE(app); }

// LLM provider config, llm-contract.md §5; persisted via io/fileStore Settings as llm*.
namespace stencil::llm {

  // browser/js/config/llm/providers.json; empty on a broken alias (configCanon test fails fast).
  inline const QJsonObject& providersCanon() {
    static const QJsonObject canon = [] {
      stencilLlmEnsureAppResources();
      QFile f(QStringLiteral(":/config/llm/providers.json"));
      return f.open(QIODevice::ReadOnly)
                 ? QJsonDocument::fromJson(f.readAll()).object()
                 : QJsonObject();
    }();
    return canon;
  }

  inline QJsonObject providerCanonEntry(const QString& provider) {
    return providersCanon().value(QStringLiteral("providers"))
        .toObject().value(provider).toObject();
  }

  // §5 defaults: stencil-server and "none" have no static URL; unknowns read ollama's.
  inline QString defaultLlmBaseUrl(const QString& provider) {
    if (provider == "stencil-server" || provider == "none") return QString();
    const QString url = providerCanonEntry(provider).value(QStringLiteral("defaultBaseUrl")).toString();
    return url.isEmpty()
               ? providerCanonEntry(QStringLiteral("ollama")).value(QStringLiteral("defaultBaseUrl")).toString()
               : url;
  }

  // Some provider's §5 default — a URL the user never typed, so a provider switch replaces it.
  inline bool isDefaultLlmBaseUrl(const QString& url) {
    if (url.isEmpty()) return false;
    for (const auto p : providersCanon().value(QStringLiteral("providers")).toObject())
      if (p.toObject().value(QStringLiteral("defaultBaseUrl")).toString() == url) return true;
    return false;
  }

  // What an anthropic turn with no session key (never entered, expired or forgotten) reads (§6.5).
  inline constexpr char NO_SESSION_KEY_TEXT[] = "no API key for this session";

  inline QString llmProviderDisplayName(const QString& provider) {
    return providerCanonEntry(provider).value(QStringLiteral("displayName")).toString();
  }

  // 120 s stands in if the canon is unreadable.
  inline int llmChatTimeoutMs() {
    return providersCanon().value(QStringLiteral("timeouts")).toObject()
               .value(QStringLiteral("chatSeconds")).toInt(120) * 1000;
  }

  // A reachability probe or model list resolves fast (the settings status dot); 3 s stands in.
  inline int llmProbeTimeoutMs() {
    return providersCanon().value(QStringLiteral("timeouts")).toObject()
        .value(QStringLiteral("probeMs")).toInt(3000);
  }

  // How long an entered anthropic key is held (providers.json sessionKey.ttlMinutes; 720 stands in).
  inline int sessionKeyTtlMinutes() {
    return providerCanonEntry(QStringLiteral("anthropic")).value(QStringLiteral("sessionKey")).toObject()
        .value(QStringLiteral("ttlMinutes")).toInt(720);
  }

  struct LlmSettings {
    // First-run default: the assistant ships OFF (contract §5) until the user picks
    // a provider — browser/js/llm/settings.js defaultSettings() parity.
    QString provider = "none";
    QString baseUrl = defaultLlmBaseUrl(QStringLiteral("none"));
    // Empty = provider default.
    QString model;
    // openai-compat: optional (LM Studio needs none); anthropic: the session key, never persisted.
    QString apiKey;
    // stencil-server only; empty = the FIRST saved connection (net/connectionStore).
    QString serverUrl;
  };

  // "none" is a valid LOCAL-ONLY value (assistant off) — never a wire value.
  inline bool isKnownLlmProvider(const QString& provider) {
    return provider == "none" || provider == "ollama" || provider == "openai-compat" ||
           provider == "anthropic" || provider == "stencil-server";
  }

}  // namespace stencil::llm
