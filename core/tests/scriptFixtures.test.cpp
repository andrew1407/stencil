#include "doctest.h"

#include "scriptDump.hpp"
#include "scriptProgram.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

// Walks the shared corpus in browser/js/config/script/fixtures/, the same files every
// surface's walker replays. Mirrors browser/tests/scriptFixtures.test.js.
using namespace stencil::core::script;

namespace {

  std::filesystem::path repoRoot() {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 16; ++i) {
      if (std::filesystem::exists(dir / "CLAUDE.md")) return dir;
      if (!dir.has_parent_path() || dir.parent_path() == dir) break;
      dir = dir.parent_path();
    }
    return {};
  }

  std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }

}  // namespace

TEST_CASE("script fixtures: every .stc matches its recorded dump and diagnostics") {
  const std::filesystem::path root = repoRoot();
  REQUIRE_MESSAGE(!root.empty(), "could not locate the repo root (CLAUDE.md)");
  const std::filesystem::path dir = root / "browser/js/config/script/fixtures";
  REQUIRE_MESSAGE(std::filesystem::is_directory(dir), "the script fixture corpus is missing");

  std::vector<std::filesystem::path> cases;
  for (const auto& entry : std::filesystem::directory_iterator(dir))
    if (entry.path().extension() == ".stc") cases.push_back(entry.path());
  std::sort(cases.begin(), cases.end());
  CHECK_MESSAGE(cases.size() >= 40, "the corpus shrank — a fixture was deleted?");

  for (const std::filesystem::path& stc : cases) {
    const std::string name = stc.stem().string();
    const std::string src = readFile(stc);
    const ScriptProgram program = ScriptProgram::parse(src.data(), static_cast<int>(src.size()));

    std::filesystem::path dumpPath = stc;
    dumpPath.replace_extension();
    const std::filesystem::path diagPath = dumpPath.string() + ".diag.txt";
    dumpPath = dumpPath.string() + ".dump.txt";

    REQUIRE_MESSAGE(std::filesystem::exists(dumpPath), "no .dump.txt for " << name);
    CHECK_MESSAGE(program.dump() == readFile(dumpPath), "dump mismatch for " << name);

    const std::string diags = dumpDiagnostics(program);
    const std::string expected = std::filesystem::exists(diagPath) ? readFile(diagPath) : "";
    CHECK_MESSAGE(diags == expected, "diagnostic mismatch for " << name);

    // The naming convention is load-bearing: err-* must fail, everything else must not.
    const bool shouldError = name.rfind("err-", 0) == 0;
    CHECK_MESSAGE(program.hasErrors() == shouldError, "error expectation for " << name);
  }
}

TEST_CASE("script fixtures: a parse never crashes on truncated input") {
  const std::filesystem::path root = repoRoot();
  REQUIRE(!root.empty());
  const std::string src = readFile(root / "browser/js/config/script/fixtures/tour-crop.stc");
  REQUIRE(!src.empty());
  for (std::size_t cut = 0; cut < src.size(); cut += 7) {
    const ScriptProgram p = ScriptProgram::parse(src.data(), static_cast<int>(cut));
    CHECK(p.ops().size() <= static_cast<std::size_t>(MAX_OPS));
  }
}
