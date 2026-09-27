// The registry resolved per surface — membership, order, flags, forbidden policy, surface rules,
// capability gating — and a registry core cannot trust refused with a reason, never a crash.
// Twin of browser/tests/llm/plan/planWalk.test.js on the schema side.
#include "doctest.h"

#include "opplanCorpus.hpp"
#include "planSchema.hpp"
#include "planWalk.hpp"

#include <string>
#include <vector>

using stencil::core::json::Value;
using namespace stencil::core::opplan;

namespace {
  std::vector<std::string> names(const Schema& s) {
    std::vector<std::string> out;
    for (const Entry& e : s.entries) out.push_back(e.name);
    return out;
  }

  std::string walked(const Schema& s, const std::string& text) { return walkPlan(s, text).text; }
}  // namespace

TEST_CASE("opplan schema: each surface registers its profile's ops, in prompt order") {
  const std::string registry = opplanCorpus::registryText();
  Schema cli;
  cli.load(registry, "cli", nullptr);
  REQUIRE(cli.error.empty());
  CHECK(cli.profile == "console");
  CHECK(names(cli) == std::vector<std::string>{
                          "crop", "rotate", "filter", "layout", "formula", "page", "blank", "undo",
                          "redo", "reset", "frame", "image", "save", "accent", "connect", "disconnect",
                          "reconnect", "delete", "openFile", "openUrl", "copy", "clear", "clearChat"});
  CHECK(cli.find("copy")->keys->keys.empty());
  CHECK(cli.find("openFile")->topLevelOnly);
  CHECK(cli.find("reconnect")->settings);
  CHECK(cli.hardFail);
  CHECK(cli.isForbidden("endpoint"));
  Schema py;
  py.load(registry, "pystencil", nullptr);
  CHECK(py.find("accent") == nullptr);
  CHECK(py.find("reconnect") == nullptr);
  CHECK_FALSE(py.hardFail);
  Schema desktop, browser;
  desktop.load(registry, "desktop", nullptr);
  browser.load(registry, "browser", nullptr);
  CHECK(desktop.find("openFile") != nullptr);
  CHECK(desktop.find("voiceChat") == nullptr);
  CHECK(browser.find("voiceChat") != nullptr);
  CHECK(browser.find("openFile") == nullptr);
  CHECK(desktop.surfaceRules.size() == 2);
  CHECK(browser.surfaceRules.empty());
}

TEST_CASE("opplan schema: an unwired capability turns its op into an unknown one") {
  Schema s;
  s.load(opplanCorpus::registryText(), "cli", "loadAttachment");
  REQUIRE(s.error.empty());
  CHECK(s.find("save") == nullptr);
  CHECK(s.find("image") != nullptr);
  CHECK(walked(s, R"({"reply":"x","actions":[{"op":"save"}]})").find("W_UNKNOWN_OP") != std::string::npos);
  Schema none;
  none.load(opplanCorpus::registryText(), "cli", "");
  CHECK(none.find("image") == nullptr);
  CHECK(none.find("rotate") != nullptr);
}

TEST_CASE("opplan schema: a registry core cannot trust is refused with a reason") {
  const std::string registry = opplanCorpus::registryText();
  Schema unknown;
  unknown.load(registry, "toaster", nullptr);
  CHECK(unknown.error.find("unknown surface") != std::string::npos);
  Schema broken;
  broken.load("{\"$meta\":", "cli", nullptr);
  CHECK_FALSE(broken.error.empty());
  Schema drifted;
  std::string edited = registry;
  const std::string from = "\"HEX\": \"^#[0-9a-fA-F]{6}$\"";
  REQUIRE(edited.find(from) != std::string::npos);
  edited.replace(edited.find(from), from.size(), "\"HEX\": \"^#[0-9a-f]{6}$\"");
  drifted.load(edited, "cli", nullptr);
  CHECK(drifted.error.find("HEX") != std::string::npos);
  Schema uncapped;
  std::string noCaps = registry;
  const std::string caps = "\"json\": {";
  REQUIRE(noCaps.find(caps) != std::string::npos);
  noCaps.replace(noCaps.find(caps), caps.size(), "\"jsonX\": {");
  uncapped.load(noCaps, "cli", nullptr);
  CHECK(uncapped.error.find("limits.json") != std::string::npos);
}

TEST_CASE("opplan schema: the resolved surface as JSON, with the registry's size and FNV-1a") {
  const std::string registry = opplanCorpus::registryText();
  Schema s;
  s.load(registry, "cli", nullptr);
  const Value doc = stencil::core::json::readJson(s.entriesJson()).value;
  CHECK(doc.get("entries")->items.size() == s.entries.size());
  CHECK(doc.get("entries")->items[0].get("name")->text == "crop");
  CHECK(doc.get("hardFail")->flag);
  CHECK(doc.get("registryBytes")->number == static_cast<double>(registry.size()));
  CHECK(doc.get("registryFnv1a64")->text.size() == 16);
  CHECK(doc.get("limits")->get("MAX_ACTIONS")->number == 16);
  CHECK(doc.get("defaultCustomLabel")->text == s.defaultCustomLabel);
}

TEST_CASE("opplan walk: hostile replies never crash and never recurse") {
  Schema s;
  s.load(opplanCorpus::registryText(), "desktop", nullptr);
  const std::string deep = "{\"reply\":\"x\",\"z\":" + std::string(500000, '[') + std::string(500000, ']') + "}";
  CHECK(walked(s, deep).find("E_JSON_LIMIT") != std::string::npos);
  const std::string open = "{\"reply\":\"x\",\"z\":" + std::string(500000, '{');
  CHECK(walkPlan(s, open).status == Status::CHAT_ONLY);
  CHECK(walkPlan(s, std::string("\xff\xfe{\"reply\":\"\xed\xa0\x80\"}")).status == Status::VALID);
  CHECK(walkPlan(s, "").status == Status::CHAT_ONLY);
}
