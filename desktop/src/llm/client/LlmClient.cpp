#include "LlmClient.hpp"
#include "llmClientShared.hpp"

#include "connectionStore.hpp"
#include "opRegistry.hpp"
#include "ServerClient.hpp"

#include <utility>

namespace stencil::llm {


  QString LlmClient::savedServerToken(const QString& serverUrl) {
    const QString base = net::ServerClient::normalizeBase(serverUrl);
    const auto saved = net::connectionStore::loadSavedServers();
    for (const auto& s : saved)
      if (net::ServerClient::normalizeBase(s.url) == base) return s.token;
    return QString();
  }

  LlmClient::LlmClient(LlmTransport* transport)
      : transport(transport), tokenResolver(&LlmClient::savedServerToken) {}

  void LlmClient::setServerTokenResolver(
      std::function<QString(const QString& serverUrl)> resolver) {
    if (resolver) tokenResolver = std::move(resolver);
  }

  void LlmClient::abort() { transport->abortActive(); }

  QString LlmClient::systemPrompt(const QString& suffix) {
    // §4 prose core + the ops section assembled from the §13 op registry
    // (the §10 editor block sits between the frame and image bullets).
    QString s = assembledSystemPrompt();
    if (!suffix.isEmpty()) s += QStringLiteral("\n\n") + suffix;
    return s;
  }

  void LlmClient::chat(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                       const QString& systemSuffix, std::function<void(LlmReply)> done) {
    // Local-only "assistant off" (contract §5 note): a typed config error,
    // never a transport call.
    if (cfg.provider == QLatin1String("none")) {
      done(failReply(LlmFailure::OFF,
                     QStringLiteral("The assistant is turned off — choose a provider "
                                    "to enable it.")));
      return;
    }
    // A mapping is picked by the provider's providers.json `wire`, never by its name (§6).
    using ChatWire = void (LlmClient::*)(const LlmSettings&, const QVector<ChatMessage>&,
                                         const QString&, std::function<void(LlmReply)>);
    static const std::pair<const char*, ChatWire> CHAT_WIRES[] = {
        {"ollama", &LlmClient::chatOllama},
        {"openai", &LlmClient::chatOpenAi},
        {"server", &LlmClient::chatServer},
        {"anthropic", &LlmClient::chatAnthropic},
    };
    const QString wire = providerCanonEntry(cfg.provider).value(QStringLiteral("wire")).toString();
    for (const auto& [name, fn] : CHAT_WIRES)
      if (wire == QLatin1String(name)) return (this->*fn)(cfg, messages, systemPrompt(systemSuffix), std::move(done));
    done(failReply(LlmFailure::BAD_RESPONSE,
                   QStringLiteral("Unknown LLM provider \"%1\"").arg(cfg.provider)));
  }
}  // namespace stencil::llm

