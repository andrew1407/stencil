#pragma once
#include "llmSettings.hpp"
#include "llmTransport.hpp"
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

// LLM chat client, llm-contract.md §6 wire mappings; browser twin: js/llm/llmClient.js.
namespace stencil::llm {

  // The system prompt (§4) is ASSEMBLED from the §13 op registry; clients may append a suffix, never prepend.
  // Already downscaled (≤1568 px long edge, §7) and base64-encoded.
  struct ChatImage {
    QString mediaType = "image/png";
    QByteArray data;
  };

  // Replayed in full each call — all providers are stateless.
  struct ChatMessage {
    QString role;
    QString text;
    QVector<ChatImage> images;
  };

  // Truncated/Refusal are NEVER parsed as plans (§6.3). Off = provider "none" (browser kind
  // "config", with the Configure CTA); Disabled = the server's 503 llmDisabled (no CTA);
  // Expired = the SERVER PROVIDER refused our session (401/403); a local 401 stays Http.
  enum class LlmFailure { NONE, TRANSPORT, HTTP, BAD_RESPONSE, TRUNCATED, REFUSAL, DISABLED, OFF, EXPIRED };

  struct LlmReply {
    bool ok = false;
    QString text;
    QString model;
    QString stopReason;
    LlmFailure failure = LlmFailure::NONE;
    QString error;
    // Expired only: the server whose session lapsed, for the "Reconnect to <host>" action.
    QString expiredHost;
  };

  struct LlmProbeResult {
    bool ok = false;
    QString detail;
    QString model;
  };

  class LlmClient {
   public:
    // `transport` is borrowed, not owned.
    explicit LlmClient(LlmTransport* transport);

    // Default resolver: savedServerToken; MainWindow installs one preferring the LIVE token.
    void setServerTokenResolver(std::function<QString(const QString& serverUrl)> resolver);

    static QString savedServerToken(const QString& serverUrl);

    // `systemSuffix` is appended to the canonical system prompt ("" = none).
    void chat(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
              const QString& systemSuffix, std::function<void(LlmReply)> done);

    // The pending `done` completes with a canceled transport error.
    void abort();

    // ollama /api/version, openai-compat /models, stencil-server /llm/info (`enabled:false` = not ok).
    void probe(const LlmSettings& cfg, std::function<void(LlmProbeResult)> done);

    // Any failure / missing auth ⇒ empty list, never an error (browser listModels).
    void listModels(const LlmSettings& cfg, std::function<void(QStringList)> done);

    static QString systemPrompt(const QString& suffix);

   private:
    void chatOllama(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                    const QString& system, std::function<void(LlmReply)> done);
    void chatOpenAi(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                    const QString& system, std::function<void(LlmReply)> done);
    void chatServer(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                    const QString& system, std::function<void(LlmReply)> done);

    LlmTransport* transport_;
    std::function<QString(const QString&)> tokenResolver_;
  };

}  // namespace stencil::llm
