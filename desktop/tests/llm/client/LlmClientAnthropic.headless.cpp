// The anthropic wire off the shared corpus: its §6.4 probe and model list with the session-key
// headers, and a missing key or a plain-http host off loopback that sends nothing at all.
#include "llmClientParts.hpp"

namespace llmclient {

  namespace {
    LlmSettings anthropicCfg(const QString& key) {
      LlmSettings c;
      c.provider = "anthropic";
      c.baseUrl = defaultLlmBaseUrl("anthropic") + "/";
      c.apiKey = key;
      return c;
    }
  }  // namespace

  void checkAnthropic() {
    std::printf("anthropic:\n");
    check(defaultLlmBaseUrl("anthropic") == "https://api.anthropic.com", "anthropic default URL (§5)");
    check(isKnownLlmProvider("anthropic"), "anthropic is a known provider");
    {
      MockTransport t;
      t.response = R"({"data":[{"type":"model","id":"claude-opus-5"},{"type":"model","id":"claude-haiku-4-5"}]})";
      LlmClient client(&t);
      LlmProbeResult got;
      client.probe(anthropicCfg("sk-ant-probe-1234"), [&](LlmProbeResult r) { got = r; });
      check(t.method == "GET" && t.url.toString() == "https://api.anthropic.com/v1/models",
            "anthropic probes GET {base}/v1/models");
      check(t.header("x-api-key") == "sk-ant-probe-1234" && t.header("anthropic-version") == "2023-06-01",
            "the probe carries the §6.5 headers");
      check(t.header("Authorization").isEmpty() &&
                t.header("anthropic-dangerous-direct-browser-access").isEmpty(),
            "no Authorization, and no browser-only header");
      check(got.ok && got.detail == "claude-opus-5", "the first model id is the probe detail");
      QStringList names;
      client.listModels(anthropicCfg("sk-ant-probe-1234"), [&](QStringList n) { names = n; });
      check(names == QStringList({"claude-opus-5", "claude-haiku-4-5"}), "models = data[].id");
    }
    {
      MockTransport t;
      t.status = 401;
      t.response = R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})";
      LlmClient client(&t);
      LlmProbeResult got;
      client.probe(anthropicCfg("sk-ant-bad"), [&](LlmProbeResult r) { got = r; });
      check(!got.ok && got.detail == "HTTP 401", "a refused key is a failed probe reading HTTP 401");
    }
    {
      // No key: the probe, the model list and the chat all fail without a request.
      MockTransport t;
      LlmClient client(&t);
      LlmProbeResult probe;
      client.probe(anthropicCfg(QString()), [&](LlmProbeResult r) { probe = r; });
      check(!probe.ok && probe.detail == "no API key for this session", "no key: a failed probe");
      QStringList names{"stale"};
      client.listModels(anthropicCfg(QString()), [&](QStringList n) { names = n; });
      check(names.isEmpty(), "no key: an empty model list");
      LlmReply reply;
      client.chat(anthropicCfg(QString()), sampleMessages(), QString(), [&](LlmReply r) { reply = r; });
      check(!reply.ok && reply.failure == LlmFailure::DISABLED &&
                reply.error == "no API key for this session",
            "no key: a typed disabled error");
      check(t.method.isEmpty() && t.url.isEmpty(), "no key: nothing was sent anywhere");
    }
    {
      // Plain http: [::1] carries the key; a LAN host is refused as disabled before any request.
      MockTransport t;
      t.response = R"({"stop_reason":"end_turn","content":[{"type":"text","text":"ok"}]})";
      LlmClient client(&t);
      LlmSettings cfg = anthropicCfg("sk-ant-local-1234");
      cfg.baseUrl = "http://[::1]:8787";
      LlmReply reply;
      client.chat(cfg, sampleMessages(), QString(), [&](LlmReply r) { reply = r; });
      check(reply.ok && t.url.toString() == "http://[::1]:8787/v1/messages" && t.header("x-api-key") == "sk-ant-local-1234",
            "plain http to [::1] carries the key");
      MockTransport lanT;
      LlmClient lan(&lanT);
      cfg.baseUrl = "http://192.168.1.5";
      const QString refusal = "refusing to send the API key to '192.168.1.5' over plain http — use https";
      lan.chat(cfg, sampleMessages(), QString(), [&](LlmReply r) { reply = r; });
      check(!reply.ok && reply.failure == LlmFailure::DISABLED && reply.error == refusal, "a LAN host: a typed disabled refusal");
      LlmProbeResult probe;
      lan.probe(cfg, [&](LlmProbeResult r) { probe = r; });
      QStringList names{"stale"};
      lan.listModels(cfg, [&](QStringList n) { names = n; });
      check(!probe.ok && probe.detail == refusal && names.isEmpty(), "a LAN host: the probe fails with it, no models");
      check(lanT.method.isEmpty() && lanT.url.isEmpty(), "a LAN host: nothing was sent anywhere");
    }
    {
      MockTransport t;
      t.status = 0;
      t.transportError = "Connection refused";
      LlmClient client(&t);
      LlmReply reply;
      client.chat(anthropicCfg("sk-ant-x"), sampleMessages(), QString(), [&](LlmReply r) { reply = r; });
      check(t.url.toString() == "https://api.anthropic.com/v1/messages", "a trailing slash is trimmed");
      check(!reply.ok && reply.failure == LlmFailure::TRANSPORT &&
                reply.error == "Couldn't reach Anthropic API (Claude) at api.anthropic.com (Connection refused)",
            "a transport failure names the endpoint");
    }
  }

}  // namespace llmclient
