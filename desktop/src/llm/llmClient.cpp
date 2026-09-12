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


  QString LlmClient::savedServerToken(const QString& serverUrl) {
    const QString base = net::ServerClient::normalizeBase(serverUrl);
    const auto saved = net::connectionStore::loadSavedServers();
    for (const auto& s : saved)
      if (net::ServerClient::normalizeBase(s.url) == base) return s.token;
    return QString();
  }

  LlmClient::LlmClient(LlmTransport* transport)
      : transport_(transport), tokenResolver_(&LlmClient::savedServerToken) {}

  void LlmClient::setServerTokenResolver(
      std::function<QString(const QString& serverUrl)> resolver) {
    if (resolver) tokenResolver_ = std::move(resolver);
  }

  void LlmClient::abort() { transport_->abortActive(); }

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
    const QString system = systemPrompt(systemSuffix);
    if (cfg.provider == QLatin1String("stencil-server")) {
      chatServer(cfg, messages, system, std::move(done));
    } else if (cfg.provider == QLatin1String("openai-compat")) {
      chatOpenAi(cfg, messages, system, std::move(done));
    } else if (cfg.provider == QLatin1String("ollama")) {
      chatOllama(cfg, messages, system, std::move(done));
    } else {
      done(failReply(LlmFailure::BAD_RESPONSE,
                     QStringLiteral("Unknown LLM provider \"%1\"").arg(cfg.provider)));
    }
  }
}  // namespace stencil::llm

