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
    const QString id = provider == "openai-compat" ? provider : QStringLiteral("ollama");
    return providerCanonEntry(id).value(QStringLiteral("defaultBaseUrl")).toString();
  }

  inline QString llmProviderDisplayName(const QString& provider) {
    return providerCanonEntry(provider).value(QStringLiteral("displayName")).toString();
  }

  // 120 s stands in if the canon is unreadable.
  inline int llmChatTimeoutMs() {
    return providersCanon().value(QStringLiteral("timeouts")).toObject()
               .value(QStringLiteral("chatSeconds")).toInt(120) * 1000;
  }

  struct LlmSettings {
    QString provider = "ollama";
    QString baseUrl = defaultLlmBaseUrl(QStringLiteral("ollama"));
    // Empty = provider default.
    QString model;
    // openai-compat only; optional (LM Studio needs none).
    QString apiKey;
    // stencil-server only; empty = the FIRST saved connection (net/connectionStore).
    QString serverUrl;
  };

  // "none" is a valid LOCAL-ONLY value (assistant off) — never a wire value.
  inline bool isKnownLlmProvider(const QString& provider) {
    return provider == "none" || provider == "ollama" ||
           provider == "openai-compat" || provider == "stencil-server";
  }

}  // namespace stencil::llm
