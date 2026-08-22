#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

// Q_INIT_RESOURCE must sit outside any namespace; same pre-main guard as
// opRegistry.cpp/theme.cpp, inline because this header is the whole module.
inline void stencilLlmEnsureAppResources() { Q_INIT_RESOURCE(app); }

// LLM provider configuration — the desktop copy of the provider-config shape in
// llm-contract.md §5 (identical keys in every client; persisted through
// io/fileStore's Settings as llmProvider/llmBaseUrl/llmModel/llmApiKey/
// llmServerUrl). Header-only, like the contract's config: plain data + lookups
// served from the shared providers.json canon riding in app.qrc.
namespace stencil::llm {

  // The shared canon (browser/js/config/llm/providers.json), parsed once.
  // Empty on a broken alias — configCanon/llmSettings headless fail fast.
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

  // The contract §5 defaults table. stencil-server has no static default URL
  // (its default is the client's first configured connection, resolved by the
  // caller); the local-only "none" (assistant off — §5 note) contacts nothing,
  // so both return empty. Anything else reads the canon (ollama for unknowns —
  // ollama is the default provider).
  inline QString defaultLlmBaseUrl(const QString& provider) {
    if (provider == "stencil-server" || provider == "none") return QString();
    const QString id = provider == "openai-compat" ? provider : QStringLiteral("ollama");
    return providerCanonEntry(id).value(QStringLiteral("defaultBaseUrl")).toString();
  }

  // Canon display name ("" for none/unknown — callers fall back themselves).
  inline QString llmProviderDisplayName(const QString& provider) {
    return providerCanonEntry(provider).value(QStringLiteral("displayName")).toString();
  }

  // timeouts.chatSeconds → the chat transfer timeout in ms (probe timeouts
  // stay surface-local). 120 s stands in if the canon is unreadable.
  inline int llmChatTimeoutMs() {
    return providersCanon().value(QStringLiteral("timeouts")).toObject()
               .value(QStringLiteral("chatSeconds")).toInt(120) * 1000;
  }

  // Providers (contract §5): "ollama" | "openai-compat" | "stencil-server".
  struct LlmSettings {
    QString provider = "ollama";
    // Applies to ollama / openai-compat. Pre-filled with the provider default;
    // the user can edit every URL by hand.
    QString baseUrl = defaultLlmBaseUrl(QStringLiteral("ollama"));
    // Empty = user hasn't picked one (ollama), the server serves whatever is
    // loaded (openai-compat), or the collaboration server's default.
    QString model;
    // openai-compat only; optional (LM Studio needs none). Sent as
    // "Authorization: Bearer <apiKey>".
    QString apiKey;
    // stencil-server only: which configured collaboration server proxies
    // Anthropic. Empty = resolve at use time to the FIRST saved connection
    // (net/connectionStore) — the contract's default row.
    QString serverUrl;
  };

  // "none" is a valid LOCAL-ONLY value (assistant off) — never a wire value.
  inline bool isKnownLlmProvider(const QString& provider) {
    return provider == "none" || provider == "ollama" ||
           provider == "openai-compat" || provider == "stencil-server";
  }

}  // namespace stencil::llm
