#include "doctest.h"

#include "scriptProgram.hpp"
#include "scriptTypes.hpp"

#include <string>

// The caps in scriptTypes.hpp, each proved by the smallest input that trips it.
// Mirrors browser/tests/scriptLimits.test.js: both engines must refuse the same way.
using namespace stencil::core::script;

namespace {

  ScriptProgram parse(const std::string& src) {
    return ScriptProgram::parse(src.data(), static_cast<int>(src.size()));
  }

  // The one error code a capped script reports, or "" when it reports none.
  std::string onlyErrorCode(const ScriptProgram& p) {
    std::string found;
    for (const Diagnostic& d : p.diagnostics()) {
      if (d.severity != Severity::ERROR) continue;
      if (found.empty()) found = d.code;
      else if (found != d.code) return "<several>";
    }
    return found;
  }

  std::string repeat(const std::string& unit, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += unit;
    return out;
  }

  std::string numbered(const std::string& before, const std::string& after, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += before + std::to_string(i) + after;
    return out;
  }

}  // namespace

TEST_CASE("a script past MAX_LINES is refused before anything is parsed") {
  const ScriptProgram p = parse(repeat("\n", MAX_LINES + 1) + "@filter bw\n");
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_LINES");
}

TEST_CASE("a script past MAX_OPS is refused while it lowers") {
  const ScriptProgram p = parse(repeat("@filter bw\n", MAX_OPS + 1));
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_OPS");
}

TEST_CASE("more than MAX_BLOCKS @source blocks is an error") {
  const ScriptProgram p = parse(numbered("@source a", ".png:\n  @filter bw\n", MAX_BLOCKS + 1));
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_BLOCKS");
}

TEST_CASE("more than MAX_TEMPLATES @stencil definitions is an error") {
  const ScriptProgram p = parse(numbered("@stencil t", ":\n  @filter bw\n", MAX_TEMPLATES + 1));
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_TEMPLATES");
}

TEST_CASE("a line past MAX_POINTS_PER_LINE is an error") {
  std::string src = "@line";
  for (int i = 0; i <= MAX_POINTS_PER_LINE; ++i)
    src += " (" + std::to_string(i) + "," + std::to_string(i) + ")";
  const ScriptProgram p = parse(src + "\n");
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_POINTS");
}

TEST_CASE("a @source spec past MAX_SOURCE_CHARS is an error") {
  const ScriptProgram p =
      parse("@source " + repeat("a", MAX_SOURCE_CHARS + 1) + ".png:\n  @filter bw\n");
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_SOURCE");
}

TEST_CASE("a script right at each cap is accepted") {
  // Exactly MAX_LINES lines: the last one carries no newline of its own.
  CHECK_FALSE(parse(repeat("\n", MAX_LINES - 1) + "@filter bw").hasErrors());
  CHECK_FALSE(parse(repeat("@filter bw\n", MAX_OPS)).hasErrors());
  CHECK_FALSE(parse(numbered("@source a", ".png:\n  @filter bw\n", MAX_BLOCKS)).hasErrors());
}
