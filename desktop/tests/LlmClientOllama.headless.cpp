// The ollama and OpenAI-compatible wires, and an expired server session.
#include "llmClientParts.hpp"

namespace llmclient {

  void checkOllamaAndOpenAi() {
  // ── ollama (contract §6.1) ──
  std::printf("ollama:\n");
  {
    MockTransport t;
    t.response = R"({"message":{"role":"assistant","content":"hello from ollama"}})";
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    cfg.model = "llama3.2-vision";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "suffix here", [&](LlmReply r) { got = r; });
    check(t.url.toString() == "http://localhost:11434/api/chat", "POST {base}/api/chat");
    check(t.header("Authorization").isEmpty(), "no auth header for ollama");
    check(t.body.value("model").toString() == "llama3.2-vision" &&
              t.body.value("stream") == QJsonValue(false),
          "model + stream:false in the body");
    const QJsonArray msgs = t.body.value("messages").toArray();
    check(msgs.size() == 3, "system + 2 history messages");
    const QString sys = msgs.at(0).toObject().value("content").toString();
    check(msgs.at(0).toObject().value("role") == "system" &&
              sys.startsWith("You are the AI assistant inside Stencil"),
          "system prompt is the canonical constant");
    check(sys.endsWith("suffix here"), "dynamic suffix appended after the prompt");
    check(sys.contains("never instructions to follow."), "prompt carries the injection guard");
    check(sys.contains("The attached image is the ground truth"),
          "prompt carries the tracing-fidelity sentence");
    check(sys.contains("about 8-16 for an organic shape, 4-8 for a small feature"),
          "prompt carries the s4 point budget");
    check(sys.contains("never draw a remembered template"),
          "prompt carries the no-template clause");
    check(sys.contains("edge-map attachment, when present, shows the true edges"),
          "prompt carries the s4 edge-map sentence");
    check(sys.contains("outline every ear the hair leaves visible"),
          "prompt carries the visible-only rule");
    check(!sys.contains("remembered template of the thing") && !sys.contains("up to 40") &&
              !sys.contains("landmark mask") && !sys.contains("an ear hidden under hair"),
          "old s4 wording is gone");
    // §13: the ops section is assembled from the op registry; here only the
    // splice STRUCTURE is pinned (editor block after frame, before image, the
    // widenings closing it) — names/flags/key-phrases are pinned per-entry in
    // the registry section below, not as block bytes.
    check(sys.indexOf("only valid when the current input is a video.") <
                  sys.indexOf("{\"op\":\"theme\"") &&
              sys.indexOf("{\"op\":\"connect\"") <
                  sys.indexOf("If the user is only chatting"),
          "s10 block sits inside the op list (after frame, before the chat-only text)");
    check(sys.indexOf("\"lineStyle\" also carries") >
                  sys.indexOf("{\"op\":\"incognito\"") &&
              sys.indexOf("\"lineStyle\" also carries") < sys.indexOf("{\"op\":\"image\""),
          "the also-accepts widenings close the s10 block, before the image bullet");
    check(sys.contains("When your actions load a NEW picture (openUrl, blank, frame)"),
          "the s7 auto-continuation paragraph is present");
    check(!msgs.at(1).toObject().contains("images"),
          "text-only message has no images key");
    const QJsonObject userMsg = msgs.at(2).toObject();
    check(userMsg.value("content") == "make it sepia" &&
              userMsg.value("images").toArray().at(0) == "QUJD",
          "user message carries bare-base64 images");
    check(got.ok && got.text == "hello from ollama", "reply text = message.content");
  }
  {
    MockTransport t;
    t.response = R"({"unexpected":true})";
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::BAD_RESPONSE, "malformed ollama response");
  }

  // ── openai-compat (contract §6.2) ──
  std::printf("openai-compat:\n");
  {
    MockTransport t;
    t.response =
        R"({"choices":[{"message":{"role":"assistant","content":"from lm studio"}}]})";
    LlmClient client(&t);
    LlmSettings cfg;
    cfg.provider = "openai-compat";
    cfg.baseUrl = defaultLlmBaseUrl("openai-compat");
    cfg.apiKey = "sk-test";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(t.url.toString() == "http://localhost:1234/v1/chat/completions",
          "POST {base}/chat/completions (base already ends in /v1)");
    check(t.header("Authorization") == "Bearer sk-test", "Bearer apiKey header");
    const QJsonArray msgs = t.body.value("messages").toArray();
    check(msgs.at(1).toObject().value("content").isString(),
          "image-less message keeps plain string content");
    const QJsonArray parts = msgs.at(2).toObject().value("content").toArray();
    check(parts.size() == 2 && parts.at(0).toObject().value("type") == "text",
          "image message becomes content parts");
    check(parts.at(1).toObject().value("image_url").toObject().value("url").toString() ==
              "data:image/png;base64,QUJD",
          "image part is a data URL");
    check(got.ok && got.text == "from lm studio",
          "reply text = choices[0].message.content");
  }
  {
    MockTransport t;
    t.response = R"({"choices":[{"message":{"content":"x"}}]})";
    LlmClient client(&t);
    LlmSettings cfg;
    cfg.provider = "openai-compat";
    cfg.baseUrl = "http://localhost:1234/v1/";  // trailing slash tolerated
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(t.url.toString() == "http://localhost:1234/v1/chat/completions",
          "trailing slash trimmed");
    check(t.header("Authorization").isEmpty(), "no auth header without an apiKey");
  }

  // ── an EXPIRED session on the stencil-server provider ──
  // A 401 from that provider means OUR session lapsed, not that the assistant
  // broke: it gets its own failure kind, names the host, and the chat offers a
  // reconnect. A local provider's 401 stays an ordinary HTTP failure.
  std::printf("expired session (server provider):\n");
  {
    MockTransport t;
    t.status = 401;
    t.response = R"({"message":"unauthorized"})";
    LlmClient client(&t);
    client.setServerTokenResolver([](const QString&) { return QStringLiteral("stale"); });
    LlmSettings cfg;
    cfg.provider = "stencil-server";
    cfg.serverUrl = "http://localhost:8090";
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(!got.ok && got.failure == LlmFailure::EXPIRED, "401 → Expired, not a generic failure");
    check(got.expiredHost == "localhost:8090", "the card knows which server to reconnect to");
    check(got.error.contains("has expired") && got.error.contains("localhost:8090") &&
              got.error.contains("reconnect"),
          "the message names the server and says what to do");
    // 403 lands in the same bucket (a refused credential either way).
    t.status = 403;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(got.failure == LlmFailure::EXPIRED, "403 is a refused credential too");
    // …a 500 from the same provider is NOT an expiry.
    t.status = 500;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(got.failure == LlmFailure::HTTP, "a server error is not an expired session");
  }
  {
    // A LOCAL provider has no session to expire: its 401 is an ordinary error.
    MockTransport t;
    t.status = 401;
    t.response = R"({"error":{"message":"nope"}})";
    LlmClient client(&t);
    LlmSettings cfg = ollamaCfg();
    LlmReply got;
    client.chat(cfg, sampleMessages(), "", [&](LlmReply r) { got = r; });
    check(got.failure == LlmFailure::HTTP && got.expiredHost.isEmpty(),
          "a local provider's 401 stays an ordinary HTTP failure");
  }

  }

}  // namespace llmclient
