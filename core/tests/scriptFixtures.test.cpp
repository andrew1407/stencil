#include "doctest.h"

#include "scriptDump.hpp"
#include "scriptProgram.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Walks the shared corpus in browser/js/config/script/fixtures/cases.txt, the same file
// every surface's walker replays. Mirrors browser/tests/scriptFixtures.test.js.
using namespace stencil::core::script;

namespace {

  struct Case {
    std::string name;
    std::string script;
    std::string dump;
    std::string diagnostics;
  };

  std::filesystem::path repoRoot() {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 16; ++i) {
      if (std::filesystem::exists(dir / "CLAUDE.md")) return dir;
      if (!dir.has_parent_path() || dir.parent_path() == dir) break;
      dir = dir.parent_path();
    }
    return {};
  }

  // A section ends at the next marker, so the blank line before it (and the file's own
  // final newline) belong to the file, not to the case.
  std::string joinBody(std::vector<std::string>& body) {
    while (!body.empty() && body.back().empty()) body.pop_back();
    std::string out;
    for (const std::string& line : body) {
      out += line;
      out.push_back('\n');
    }
    return out;
  }

  std::vector<Case> readCases(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<Case> cases;
    std::vector<std::string> body;
    std::string line, section;
    Case current;
    bool open = false;

    auto flush = [&]() {
      if (!open) return;
      if (section == "script") current.script = joinBody(body);
      else if (section == "dump") current.dump = joinBody(body);
      else if (section == "diagnostics") current.diagnostics = joinBody(body);
      body.clear();
    };

    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.rfind("=== ", 0) == 0) {
        flush();
        if (open) cases.push_back(current);
        current = Case{};
        current.name = line.substr(4);
        section.clear();
        open = true;
        continue;
      }
      if (!open) continue;  // the file's own header comment
      if (line == "--- script" || line == "--- dump" || line == "--- diagnostics") {
        flush();
        section = line.substr(4);
        continue;
      }
      if (!section.empty()) body.push_back(line);
    }
    flush();
    if (open) cases.push_back(current);
    return cases;
  }

}  // namespace

TEST_CASE("script fixtures: every case matches its recorded dump and diagnostics") {
  const std::filesystem::path root = repoRoot();
  REQUIRE_MESSAGE(!root.empty(), "could not locate the repo root (CLAUDE.md)");
  const std::filesystem::path path = root / "browser/js/config/script/fixtures/cases.txt";
  REQUIRE_MESSAGE(std::filesystem::exists(path), "the script fixture corpus is missing");

  const std::vector<Case> cases = readCases(path);
  CHECK_MESSAGE(cases.size() >= 40, "the corpus shrank — a case was deleted?");

  for (const Case& c : cases) {
    const ScriptProgram program =
        ScriptProgram::parse(c.script.data(), static_cast<int>(c.script.size()));
    CHECK_MESSAGE(program.dump() == c.dump, "dump mismatch for " << c.name);
    CHECK_MESSAGE(dumpDiagnostics(program) == c.diagnostics, "diagnostic mismatch for " << c.name);

    // The naming convention is load-bearing: err-* must fail, everything else must not.
    const bool shouldError = c.name.rfind("err-", 0) == 0;
    CHECK_MESSAGE(program.hasErrors() == shouldError, "error expectation for " << c.name);
  }
}

TEST_CASE("script fixtures: a parse never crashes on truncated input") {
  const std::filesystem::path root = repoRoot();
  REQUIRE(!root.empty());
  const std::vector<Case> cases = readCases(root / "browser/js/config/script/fixtures/cases.txt");
  std::string src;
  for (const Case& c : cases)
    if (c.name == "tour-crop") src = c.script;
  REQUIRE(!src.empty());

  for (std::size_t cut = 0; cut < src.size(); cut += 7) {
    const ScriptProgram p = ScriptProgram::parse(src.data(), static_cast<int>(cut));
    CHECK(p.ops().size() <= static_cast<std::size_t>(MAX_OPS));
  }
}
