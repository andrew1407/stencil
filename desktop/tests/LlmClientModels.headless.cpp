// Listing models, aborting a request in flight, and the error paths.
#include "llmClientParts.hpp"

namespace llmclient {

  void checkModelsAndErrors() {
  // ── model suggestions (settings UI listModels; browser parity) ──
  std::printf("listModels:\n");
  {
    MockTransport t;
    t.response = R"({"models":[{"name":"llava"},{"name":"qwen2.5vl"},{}]})";
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    QStringList got{QStringLiteral("sentinel")};
    client.listModels(cfg, [&](QStringList l) { got = l; });
    check(t.method == "GET" && t.url.toString() == "http://localhost:11434/api/tags",
          "ollama lists GET {base}/api/tags");
    check(got == QStringList({"llava", "qwen2.5vl"}),
          "ollama models[].name collected (nameless entries skipped)");
  }
  {
    MockTransport t;
    t.response = R"({"object":"list","data":[{"id":"m-1"},{"id":"m-2"},{"x":1}]})";
    LlmClient client(&t);
    LlmSettings cfg;
    cfg.provider = "openai-compat";
    cfg.baseUrl = defaultLlmBaseUrl("openai-compat");
    cfg.apiKey = "sk-list";
    QStringList got;
    client.listModels(cfg, [&](QStringList l) { got = l; });
    check(t.method == "GET" && t.url.toString() == "http://localhost:1234/v1/models",
          "openai-compat lists GET {base}/models");
    check(t.header("Authorization") == "Bearer sk-list", "listing carries the apiKey");
    check(got == QStringList({"m-1", "m-2"}), "openai-compat data[].id collected");
  }
  {
    MockTransport t;
    t.response = R"({"enabled":true,"model":"prox-default"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    QStringList got;
    client.listModels(cfg, [&](QStringList l) { got = l; });
    check(t.method == "GET" && t.url.toString() == "https://s.example.com/llm/info",
          "stencil-server lists GET /llm/info");
    check(got == QStringList({"prox-default"}), "server-side model becomes the one suggestion");
  }
  {
    MockTransport t;
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QString(); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    QStringList got{QStringLiteral("sentinel")};
    client.listModels(cfg, [&](QStringList l) { got = l; });
    check(got.isEmpty() && t.url.isEmpty(), "token-less server listing is empty, no request");
  }
  {
    MockTransport t;
    t.status = 500;
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    QStringList got{QStringLiteral("sentinel")};
    client.listModels(cfg, [&](QStringList l) { got = l; });
    check(got.isEmpty(), "listing failure is an empty list, not an error");
  }

  // ── stop (transport abort hook) ──
  std::printf("abort:\n");
  {
    struct AbortableMock : MockTransport {
      bool aborted = false;
      void abortActive() override { aborted = true; }
    };
    AbortableMock t;
    LlmClient client(&t);
    client.abort();
    check(t.aborted, "abort() reaches the transport's abortActive hook");
  }

  // ── shared error paths ──
  std::printf("errors:\n");
  {
    MockTransport t;
    t.status = 0;
    t.transportError = "connection refused";
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::TRANSPORT &&
              got.error.startsWith("Couldn't reach ") &&
              got.error.contains("(connection refused)"),
          "transport failure surfaced in the browser's is-it-running voice");
  }
  {
    MockTransport t;
    t.status = 404;
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::HTTP && got.error.contains("404"),
          "HTTP error surfaced with the status");
  }
  {
    MockTransport t;
    LlmClient client(&t);
    LlmSettings cfg;
    cfg.provider = "something-else";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.error.contains("Unknown LLM provider"), "unknown provider rejected");
  }

  }

}  // namespace llmclient
