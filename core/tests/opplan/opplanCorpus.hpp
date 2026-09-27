#pragma once
#include "jsText.hpp"
#include "jsonReader.hpp"
#include "jsonWriter.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

// The op-plan corpus as the core suites read it: the canonical registry and fixtures under
// browser/js/config/llm/, and each case's text built the way genOpPlanFixtures.mjs builds it.
namespace opplanCorpus {

  inline std::filesystem::path repoRoot() {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 16; ++i) {
      if (std::filesystem::exists(dir / "CLAUDE.md")) return dir;
      if (!dir.has_parent_path() || dir.parent_path() == dir) break;
      dir = dir.parent_path();
    }
    return {};
  }

  inline std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
  }

  inline std::filesystem::path llmDir() { return repoRoot() / "browser" / "js" / "config" / "llm"; }

  inline std::string registryText() { return slurp(llmDir() / "opRegistry.json"); }

  inline stencil::core::json::Value readFile(const std::filesystem::path& p) {
    return stencil::core::json::readJson(slurp(p)).value;
  }

  inline std::string fromBase64(const std::string& s) {
    std::string out;
    unsigned val = 0;
    int bits = -8;
    for (unsigned char c : s) {
      int d = -1;
      if (c >= 'A' && c <= 'Z') d = c - 'A';
      else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
      else if (c >= '0' && c <= '9') d = c - '0' + 52;
      else if (c == '+') d = 62;
      else if (c == '/') d = 63;
      if (d < 0) continue;
      val = ((val << 6) | static_cast<unsigned>(d)) & 0xFFFFFFu;
      bits += 6;
      if (bits >= 0) {
        out.push_back(static_cast<char>((val >> bits) & 0xFF));
        bits -= 8;
      }
    }
    return out;
  }

  // A case's reply bytes: `inputBase64` raw, else `parts`, else `input` verbatim or as
  // JSON.stringify writes it — a lone surrogate then arrives as TextEncoder's U+FFFD.
  inline std::string caseText(const stencil::core::json::Value& c) {
    using namespace stencil::core::json;
    if (const Value* b = c.get("inputBase64")) return fromBase64(b->text);
    std::string text;
    if (const Value* parts = c.get("parts")) {
      for (const Value& p : parts->items)
        for (int i = 0; i < static_cast<int>(p.items[1].number); ++i) text += p.items[0].text;
    } else if (const Value* in = c.get("input")) {
      text = in->isString() ? in->text : toJson(*in, LoneSurrogates::ESCAPE);
    }
    return wellFormed(text);
  }

  // name → case, per source bundle ("hand", "generated", "oracle").
  inline std::map<std::string, std::map<std::string, stencil::core::json::Value>> sources() {
    std::map<std::string, std::map<std::string, stencil::core::json::Value>> out;
    const std::map<std::string, std::filesystem::path> files{
        {"hand", llmDir() / "fixtures/opPlan/cases.json"},
        {"generated", llmDir() / "fixtures/opPlan/generated/cases.json"},
        {"oracle", llmDir() / "fixtures/opPlan/oracle/inputs.json"},
    };
    for (const auto& [source, path] : files) {
      stencil::core::json::Value doc = readFile(path);
      if (const stencil::core::json::Value* cases = doc.get("cases"))
        for (const stencil::core::json::Value& c : cases->items)
          if (const stencil::core::json::Value* name = c.get("name")) out[source][name->text] = c;
    }
    return out;
  }

}  // namespace opplanCorpus
