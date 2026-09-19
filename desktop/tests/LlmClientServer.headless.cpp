// The stencil-server provider: its wire, its auth and the replies it returns.
#include "llmClientParts.hpp"

namespace llmclient {

  void checkStencilServer() {
  // ── stencil-server (contract §6.3) ──
  std::printf("stencil-server:\n");
  {
    MockTransport t;
    t.response =
        R"({"model":"claude-opus-5","text":"{\"reply\":\"hi\"}","stopReason":"end_turn"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString& url) {
      return url.contains("stencil.example.com") ? QStringLiteral("tok-123") : QString();
    });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://stencil.example.com:8090";
    cfg.model = "";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(t.url.toString() == "https://stencil.example.com:8090/llm/chat",
          "POST {serverUrl}/llm/chat");
    check(t.header("Authorization") == "Bearer tok-123",
          "existing bearer token from the resolver");
    check(t.body.value("system").toString().startsWith("You are the AI assistant"),
          "system rides the request body");
    const QJsonArray msgs = t.body.value("messages").toArray();
    check(msgs.at(0).toObject().value("text") == "prior reply" &&
              msgs.at(0).toObject().value("role") == "assistant",
          "history uses role/text fields");
    const QJsonObject img =
        msgs.at(1).toObject().value("images").toArray().at(0).toObject();
    check(img.value("mediaType") == "image/png" && img.value("data") == "QUJD",
          "images use {mediaType,data} blocks");
    check(got.ok && got.model == "claude-opus-5" && got.stopReason == "end_turn",
          "response text/model/stopReason parsed");
  }
  {
    MockTransport t;
    t.response = R"({"model":"m","text":"partial {","stopReason":"max_tokens"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::TRUNCATED,
          "max_tokens -> typed Truncated error (never parsed as a plan)");
    check(got.text == "partial {", "truncated text still carried for display");
  }
  {
    MockTransport t;
    t.response = R"({"model":"m","text":"I can't help with that","stopReason":"refusal"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::REFUSAL, "refusal -> typed Refusal error");
    check(got.error.contains("can't help"), "refusal text surfaced as the error");
  }
  {
    MockTransport t;
    t.status = 503;
    t.response = R"({"code":"llmDisabled","message":"no ANTHROPIC_API_KEY"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::DISABLED,
          "503 llmDisabled -> typed Disabled failure");
    // The server's message IS the sentence (browser describeChatError "notice" parity):
    // raw, never wrapped in "<provider> at <host>:" — that wrapper is for actual
    // transport/HTTP failures, not "the provider is configured but its own operator
    // hasn't turned the LLM on".
    check(got.error == "no ANTHROPIC_API_KEY",
          "llmDisabled surfaced as the server's own message, unwrapped");
  }
  {
    // Browser unreachableText parity: the SAME server error reads the same on
    // every surface.
    MockTransport t;
    t.status = 502;
    t.response = R"({"code":"internal","message":"LLM request failed"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "http://localhost:8090";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(got.error == "Stencil server at localhost:8090: LLM request failed",
          "server error quoted in the browser's error voice");
  }
  {
    // Contract §6.3 "say the reason once": the endpoint labels the server's message,
    // nothing restates it — no "answered", no status, no upstream prose.
    MockTransport t;
    t.status = 502;
    t.response = R"({"code":"llmUpstream","message":"the LLM provider is out of credits or has no active billing"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "http://localhost:8090";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(got.error ==
              "Stencil server at localhost:8090: the LLM provider is out of credits or "
              "has no active billing",
          "an out-of-credits upstream is said once, with the host");
    check(!got.error.contains("HTTP") && !got.error.contains("answered"),
          "no status and no second framing around it");
  }
  {
    // A local provider's prose is untrusted: bounded, control-free, key/URL free.
    MockTransport t;
    t.status = 401;
    t.response =
        R"({"error":{"message":"Incorrect API key provided: sk-abcdef1234567890\nsee http://lm.local/keys"}})";
    LlmClient client(&t);
    LlmSettings cfg;
    cfg.provider = "openai-compat";
    cfg.baseUrl = "http://localhost:1234/v1";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.error.contains("sk-abcdef") && !got.error.contains("lm.local"),
          "neither the key nor a URL is echoed back");
    check(got.error.contains("Incorrect API key provided: [redacted] see [redacted]"),
          "the provider's own words survive, redacted");
  }
  {
    MockTransport t;
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QString(); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.error.contains("no token"), "missing token fails before any POST");
    check(t.url.isEmpty(), "no request went out without a token");
  }

  }

}  // namespace llmclient
