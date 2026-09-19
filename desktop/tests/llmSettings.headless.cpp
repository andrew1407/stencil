// Headless check of the LLM provider settings (src/llm/llmSettings + the io/fileStore Settings
// extension): the llm-contract.md §5 defaults table (first run shows ollama at localhost:11434;
// openai-compat pre-fills localhost:1234/v1; stencil-server resolves to a configured connection) and
// the JSON round-trip of the persisted llmProvider/llmBaseUrl/llmModel/llmApiKey/llmServerUrl keys
// (+ the windowState dock blob), through settingsToJson/settingsFromJson — no disk involved.
#include "fileStore.hpp"
#include "llmSettings.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

using namespace stencil::gui;
using namespace stencil::llm;

#include "support/check.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // ── contract §5 defaults ──
  std::printf("defaults:\n");
  {
    const LlmSettings cfg;
    check(cfg.provider == "none", "default provider is none (assistant ships off)");
    check(cfg.baseUrl.isEmpty(), "none has no default base URL");
    check(cfg.model.isEmpty() && cfg.apiKey.isEmpty() && cfg.serverUrl.isEmpty(),
          "model/apiKey/serverUrl start empty");
  }
  check(defaultLlmBaseUrl("ollama") == "http://localhost:11434", "ollama default URL");
  check(defaultLlmBaseUrl("openai-compat") == "http://localhost:1234/v1",
        "openai-compat default URL");
  check(defaultLlmBaseUrl("stencil-server").isEmpty(),
        "stencil-server has no static URL (first saved connection instead)");
  check(isKnownLlmProvider("ollama") && isKnownLlmProvider("openai-compat") &&
            isKnownLlmProvider("stencil-server") && !isKnownLlmProvider("gpt"),
        "provider whitelist");
  check(isKnownLlmProvider("none"), "local-only \"none\" (assistant off) is a known value");
  check(defaultLlmBaseUrl("none").isEmpty(), "none has no base URL — nothing is contacted");

  // First run: the persisted Settings struct pre-fills the same defaults.
  std::printf("fileStore first run:\n");
  {
    const Settings s;
    check(s.llmProvider == "none", "Settings default llmProvider = none");
    check(s.llmBaseUrl.isEmpty(), "Settings default llmBaseUrl is empty (none has no URL)");
    check(s.llmModel.isEmpty() && s.llmApiKey.isEmpty() && s.llmServerUrl.isEmpty(),
          "llm model/apiKey/serverUrl default empty (serverUrl -> first connection)");
    check(s.windowState.isEmpty(), "no saved dock state on first run");
    check(!s.saveChatsWithProject, "chat persistence ships OFF (contract §12)");
    check(s.nativeMenuBar, "the menu bar defaults to the platform's own placement");
  }

  // ── JSON round-trip ──
  std::printf("round-trip:\n");
  {
    Settings s;
    s.llmProvider = "openai-compat";
    s.llmBaseUrl = "http://localhost:1234/v1";
    s.llmModel = "qwen2.5-vl";
    s.llmApiKey = "sk-secret";
    s.llmServerUrl = "https://stencil.example.com:8090";
    s.windowState = "AAECAwQ=";
    const QJsonObject o = fileStore::settingsToJson(s);
    check(o.value("llmProvider") == "openai-compat" &&
              o.value("llmBaseUrl") == "http://localhost:1234/v1" &&
              o.value("llmModel") == "qwen2.5-vl" && o.value("llmApiKey") == "sk-secret" &&
              o.value("llmServerUrl") == "https://stencil.example.com:8090" &&
              o.value("windowState") == "AAECAwQ=",
          "llm* + windowState keys serialized");
    const Settings back = fileStore::settingsFromJson(o);
    check(back.llmProvider == s.llmProvider && back.llmBaseUrl == s.llmBaseUrl &&
              back.llmModel == s.llmModel && back.llmApiKey == s.llmApiKey &&
              back.llmServerUrl == s.llmServerUrl && back.windowState == s.windowState,
          "llm* + windowState keys round-trip");
    // The refactor didn't disturb an unrelated pre-existing key.
    check(back.themeMode == s.themeMode && back.pageSize == s.pageSize,
          "unrelated settings still round-trip");
  }
  {
    // A pre-LLM settings.json (no llm* keys) falls back to the defaults.
    QJsonObject legacy;
    legacy.insert("themeMode", "dark");
    legacy.insert("autosave", false);
    const Settings s = fileStore::settingsFromJson(legacy);
    check(s.themeMode == "dark" && !s.autosave, "legacy keys still parsed");
    check(s.llmProvider == "none" && s.llmBaseUrl.isEmpty(),
          "absent llm keys -> contract defaults");
    check(s.llmServerUrl.isEmpty() && s.windowState.isEmpty(),
          "absent serverUrl/windowState -> empty");
  }
  {
    // The local-only "none" (assistant off) round-trips through the same
    // llmProvider key.
    Settings s;
    s.llmProvider = "none";
    const Settings back = fileStore::settingsFromJson(fileStore::settingsToJson(s));
    check(back.llmProvider == "none", "llmProvider \"none\" round-trips");
  }
  {
    // Chat-persistence opt-in (§12): round-trips as a boolean; absent = off.
    Settings s;
    s.saveChatsWithProject = true;
    const Settings back = fileStore::settingsFromJson(fileStore::settingsToJson(s));
    check(back.saveChatsWithProject, "saveChatsWithProject round-trips");
    check(!fileStore::settingsFromJson(QJsonObject()).saveChatsWithProject,
          "absent saveChatsWithProject -> off");
  }
  {
    // Menu-bar placement: round-trips, and an OLDER settings.json (no such key)
    // comes back as the default rather than silently moving the menus.
    Settings s;
    s.nativeMenuBar = false;
    check(!fileStore::settingsFromJson(fileStore::settingsToJson(s)).nativeMenuBar,
          "nativeMenuBar=false round-trips");
    check(fileStore::settingsFromJson(QJsonObject()).nativeMenuBar,
          "absent nativeMenuBar -> the default (true), not false");
  }

  // ── Persisted-chat document helpers (contract §12.1) ──
  std::printf("chat doc:\n");
  {
    QJsonArray msgs;
    QJsonObject u;
    u["role"] = "user";
    u["text"] = "crop 10% off the left";
    u["images"] = QJsonArray{QStringLiteral("zzz")};  // must NOT survive
    QJsonObject a;
    a["role"] = "assistant";
    a["text"] = "Done.";
    QJsonObject junkRole;
    junkRole["role"] = "system";
    junkRole["text"] = "never persisted";
    QJsonObject noText;
    noText["role"] = "user";
    msgs.append(u);
    msgs.append(a);
    msgs.append(junkRole);
    msgs.append(noText);
    const QJsonObject doc = fileStore::buildChatDoc(msgs, 1234);
    check(doc.value("version").toInt() == fileStore::CHAT_DOC_VERSION, "doc carries version 1");
    check(doc.value("savedAt").toVariant().toLongLong() == 1234, "doc carries savedAt");
    const QJsonArray out = doc.value("messages").toArray();
    check(out.size() == 2, "junk roles / textless turns dropped");
    check(out.at(0).toObject().value("text") == "crop 10% off the left" &&
              !out.at(0).toObject().contains("images"),
          "text kept, images stripped (§12.1)");
    // parse: the round-trip yields the same sanitized messages.
    check(fileStore::parseChatDoc(doc).size() == 2, "parse accepts its own build");
    // Unknown version reads as "no saved chat", never an error.
    QJsonObject tooNew = doc;
    tooNew["version"] = 2;
    check(fileStore::parseChatDoc(tooNew).isEmpty(), "unknown version -> empty");
    check(fileStore::parseChatDoc(QJsonObject()).isEmpty(), "empty doc -> empty");
    // The §7 bound: writers trim to the most recent 32.
    QJsonArray many;
    for (int i = 0; i < 40; ++i) {
      QJsonObject m;
      m["role"] = "user";
      m["text"] = QStringLiteral("t%1").arg(i);
      many.append(m);
    }
    const QJsonArray trimmed = fileStore::buildChatDoc(many, 0).value("messages").toArray();
    check(trimmed.size() == fileStore::CHAT_DOC_MESSAGE_LIMIT, "trimmed to 32 messages");
    check(trimmed.first().toObject().value("text") == "t8", "the most recent 32 survive");
  }

  // settings.json holds llmApiKey in the clear, so it must not be group/world readable. POSIX-only: Qt
  // maps QFile::permissions() onto ACLs on Windows.
#ifdef Q_OS_UNIX
  std::printf("settings file permissions:\n");
  {
    const Settings existing = fileStore::loadSettings();
    fileStore::saveSettings(existing);   // rewrites byte-identical content
    const QFile::Permissions perms = QFile(fileStore::settingsPath()).permissions();
    check((perms & (QFile::ReadOwner | QFile::WriteOwner)) ==
              (QFile::ReadOwner | QFile::WriteOwner),
          "the owner can still read and write it");
    check(!(perms & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther |
                     QFile::WriteOther)),
          "group and other have no access (llmApiKey is plaintext)");
  }
#endif

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
