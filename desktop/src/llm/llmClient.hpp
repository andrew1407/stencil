#pragma once
#include "llmSettings.hpp"
#include "llmTransport.hpp"
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

// LLM chat client — the desktop implementation of the wire mappings in
// llm-contract.md §6 (ollama native chat, openai-compat chat/completions,
// stencil-server /llm/chat Anthropic proxy), mirrored by browser
// js/llm/llmClient.js and the other clients in the contract table. Non-streaming
// v1: one JSON POST per turn through the injected LlmTransport, so tests drive
// it with a mock transport (tests/llmClient.headless.cpp).
namespace stencil::llm {

  // Canonical system prompt (contract §4): the PROSE CORE is embedded
  // verbatim and the ops section (incl. the §10 editor block) is ASSEMBLED
  // from the §13 op registry (src/llm/opRegistry.*) — no hand-maintained ops
  // block exists anywhere. Clients may append a short dynamic suffix
  // (working-image dimensions, video frame count) — see
  // LlmClient::systemPrompt — but never prepend anything.

  // One attached image, already downscaled (≤1568 px long edge, contract §7)
  // and encoded: `mediaType` ∈ image/png|jpeg|webp|gif, `data` is base64.
  struct ChatImage {
    QString mediaType = "image/png";
    QByteArray data;  // base64 bytes (no data: prefix)
  };

  // One history message, replayed in full each call (all providers are
  // stateless). role ∈ "user" | "assistant".
  struct ChatMessage {
    QString role;
    QString text;
    QVector<ChatImage> images;
  };

  // Typed failure so stopReason max_tokens / refusal are surfaced as errors and
  // NEVER parsed as plans (contract §6.3). Disabled = provider "none"
  // (assistant off, §5 note) or the server's 503 llmDisabled (no LLM key
  // configured) — a config state, not a network failure.
  // Expired: the SERVER PROVIDER refused our session (401/403). Distinct from a
  // generic Http failure so the chat can offer a way back in instead of "the
  // request failed"; a local provider's 401 stays an ordinary Http error.
  enum class LlmFailure { None, Transport, Http, BadResponse, Truncated, Refusal, Disabled, Expired };

  struct LlmReply {
    bool ok = false;
    QString text;        // raw LLM text (feed to parseOpPlan when ok)
    QString model;       // stencil-server reports the resolved model
    QString stopReason;  // stencil-server only ("end_turn" | "max_tokens" | "refusal")
    LlmFailure failure = LlmFailure::None;
    QString error;
    // Expired only: the server whose session lapsed ("localhost:8090"), so the
    // chat can offer a "Reconnect to <host>" action beside the message.
    QString expiredHost;
  };

  // Outcome of the cheap reachability probe behind the chat dock's status dot.
  struct LlmProbeResult {
    bool ok = false;
    QString detail;  // version string / error reason ("" when nothing to add)
    QString model;   // stencil-server /llm/info reports the server-side model
  };

  class LlmClient {
   public:
    // `transport` is borrowed (not owned) so tests can inject a mock.
    explicit LlmClient(LlmTransport* transport);

    // How the stencil-server bearer token is looked up for a server origin. The
    // default resolver is savedServerToken; MainWindow installs one that
    // prefers the LIVE connection's (possibly refreshed) token.
    void setServerTokenResolver(std::function<QString(const QString& serverUrl)> resolver);

    // Token of the SAVED connection (net/connectionStore) whose normalized
    // origin matches, or "" — the default resolver, exposed so callers layering
    // a live-connection lookup can share the fallback.
    static QString savedServerToken(const QString& serverUrl);

    // One chat turn: POST per the provider's wire mapping and deliver the reply
    // text (or a typed failure) to `done`. `systemSuffix` is the short dynamic
    // context appended to the canonical system prompt ("" = none).
    void chat(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
              const QString& systemSuffix, std::function<void(LlmReply)> done);

    // Abort the in-flight chat turn (the Stop button): the pending `done`
    // completes with a canceled transport error.
    void abort();

    // Cheap reachability probe for the settings/status UI: ollama
    // GET /api/version, openai-compat GET /models (with the Bearer apiKey when
    // set), stencil-server GET /llm/info with the existing connection token
    // (also reporting the server-side model; `enabled:false` = not ok).
    void probe(const LlmSettings& cfg, std::function<void(LlmProbeResult)> done);

    // Best-effort model suggestions for the settings UI (mirrors the browser
    // listModels): ollama GET {baseUrl}/api/tags → models[].name; openai-compat
    // GET {baseUrl}/models → data[].id; stencil-server GET {serverUrl}/llm/info
    // → [model]. Any failure / missing auth ⇒ empty list, never an error.
    void listModels(const LlmSettings& cfg, std::function<void(QStringList)> done);

    // The canonical prompt + optional suffix (exposed for tests).
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
