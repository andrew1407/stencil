// Probing a provider, and the assistant turned off (provider “none”).
#include "llmClientParts.hpp"

namespace llmclient {

  void checkProbe() {
  // ── reachability probes (chat-dock status dot) ──
  std::printf("probe:\n");
  {
    MockTransport t;
    t.response = R"({"version":"0.6.2"})";
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    LlmProbeResult got;
    client.probe(cfg, [&](LlmProbeResult r) { got = r; });
    check(t.method == "GET" && t.url.toString() == "http://localhost:11434/api/version",
          "ollama probes GET /api/version");
    check(t.headers.isEmpty(), "ollama probe sends no auth");
    check(got.ok && got.detail == "0.6.2", "ollama probe ok + version surfaced");
  }
  {
    MockTransport t;
    t.response = R"({"data":[]})";
    LlmClient client(&t);
    LlmSettings cfg;
    cfg.provider = "openai-compat";
    cfg.baseUrl = defaultLlmBaseUrl("openai-compat");
    cfg.apiKey = "sk-probe";
    LlmProbeResult got;
    client.probe(cfg, [&](LlmProbeResult r) { got = r; });
    check(t.method == "GET" && t.url.toString() == "http://localhost:1234/v1/models",
          "openai-compat probes GET {base}/models");
    check(t.header("Authorization") == "Bearer sk-probe", "probe carries the apiKey");
    check(got.ok, "openai-compat probe ok on 2xx");
  }
  {
    MockTransport t;
    t.response = R"({"enabled":true,"model":"claude-opus-5"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok-9"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com:8090";
    LlmProbeResult got;
    client.probe(cfg, [&](LlmProbeResult r) { got = r; });
    check(t.method == "GET" && t.url.toString() == "https://s.example.com:8090/llm/info",
          "stencil-server probes GET /llm/info");
    check(t.header("Authorization") == "Bearer tok-9", "probe uses the connection token");
    check(got.ok && got.model == "claude-opus-5", "probe reports the server-side model");
  }
  {
    MockTransport t;
    t.response = R"({"enabled":false})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("tok"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    LlmProbeResult got;
    client.probe(cfg, [&](LlmProbeResult r) { got = r; });
    check(!got.ok && got.detail.contains("disabled"), "enabled:false probes as not ok");
  }
  {
    MockTransport t;
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QString(); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "https://s.example.com";
    LlmProbeResult got;
    got.ok = true;
    client.probe(cfg, [&](LlmProbeResult r) { got = r; });
    check(!got.ok && got.detail.contains("no token"), "token-less server probe fails locally");
    check(t.url.isEmpty(), "no probe request went out without a token");
  }
  {
    MockTransport t;
    t.status = 0;
    t.transportError = "connection refused";
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    LlmProbeResult got;
    got.ok = true;
    client.probe(cfg, [&](LlmProbeResult r) { got = r; });
    check(!got.ok && got.detail == "connection refused", "transport failure probes as not ok");
  }
  {
    MockTransport t;
    t.status = 404;
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    LlmProbeResult got;
    got.ok = true;
    client.probe(cfg, [&](LlmProbeResult r) { got = r; });
    check(!got.ok && got.detail.contains("404"), "HTTP error probes as not ok");
  }

  // ── provider "none" — assistant off (contract §5 note): a local-only value
  // that never issues a transport request from any client entry point ──
  std::printf("provider none (assistant off):\n");
  {
    MockTransport t;
    LlmClient client(&t);
    LlmSettings cfg;
    cfg.provider = "none";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::OFF &&
              got.error.contains("turned off"),
          "chat with none -> typed Off config error");
    check(t.url.isEmpty(), "chat with none never touches the transport");

    LlmProbeResult pr;
    pr.ok = true;
    client.probe(cfg, [&](LlmProbeResult r) { pr = r; });
    check(!pr.ok && pr.detail.contains("turned off"), "probe with none reports off, not ok");
    check(t.url.isEmpty(), "probe with none never touches the transport");

    QStringList models{QStringLiteral("sentinel")};
    client.listModels(cfg, [&](QStringList l) { models = l; });
    check(models.isEmpty() && t.url.isEmpty(),
          "listModels with none is empty, no request");
  }

  }

}  // namespace llmclient
