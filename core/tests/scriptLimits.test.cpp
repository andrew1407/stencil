#include "doctest.h"

#include "scriptProgram.hpp"
#include "types.hpp"

#include <string>

// The caps in types.hpp, each proved by the smallest input that trips it.
// Mirrors browser/tests/scriptLimits.test.js: both engines must refuse the same way.
using namespace stencil::core::script;

namespace {

  ScriptProgram parse(const std::string& src) {
    return ScriptProgram::parse(src.data(), static_cast<int>(src.size()));
  }

  // The one error code a capped script reports, or "" when it reports none.
  std::string onlyErrorCode(const ScriptProgram& p) {
    std::string found;
    for (const Diagnostic& d : p.getDiagnostics()) {
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

TEST_CASE("a script past MAX_TOKENS says so instead of dropping the rest silently") {
  const ScriptProgram p = parse(repeat("a ", MAX_TOKENS + 1));
  int found = 0;
  std::string message;
  for (const Diagnostic& d : p.getDiagnostics())
    if (d.code == "E_LIMIT_TOKENS") {
      ++found;
      message = d.message;
    }
  CHECK(found == 1);
  CHECK(message == "script has too many tokens (over 200000)");
}

TEST_CASE("an @undo replay that would pass MAX_OPS is refused, never multiplied") {
  const ScriptProgram p =
      parse("@source a.png:\n" + repeat("  @filter bw\n", 3000) + "  @undo 1\n  @save\n");
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_OPS");
  REQUIRE(p.getDiagnostics().size() == 1);
  CHECK(p.getDiagnostics()[0].line == 1);  // the block header, not the @save that replayed
  CHECK(p.getBlocks().empty());            // a capped block is not recorded, so nothing dumps
  CHECK(static_cast<int>(p.getOps().size()) <= MAX_OPS);
}

TEST_CASE("nested @use fan-out is bounded before the statements exist") {
  const std::string src = "@stencil ten:\n" + repeat("  @use px\n", 10) +
                          "\n@stencil hundred:\n" + repeat("  @use stencil ten\n", 10) +
                          "\n@stencil thousand:\n" + repeat("  @use stencil hundred\n", 10) +
                          "\n@source a.png:\n" + repeat("  @use stencil thousand\n", 6);
  const ScriptProgram p = parse(src);
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_OPS");
  CHECK(p.getOps().size() == 1);  // the block's open, and nothing a template fanned out
}

TEST_CASE("a fan-out that produces no statement at all is bounded too") {
  // Bodies of nothing but nested uses, bottoming out in an empty one: no statement ever
  // lands, so neither the depth cap nor the op cap can stop it — only the expansion count.
  std::string src = "@stencil t8:\n\n";
  for (int k = 7; k >= 1; --k)
    src += "@stencil t" + std::to_string(k) + ":\n" +
           repeat("  @use stencil t" + std::to_string(k + 1) + "\n", 5) + "\n";
  const ScriptProgram p = parse(src + "@use stencil t1\n");
  CHECK(p.hasErrors());
  CHECK(onlyErrorCode(p) == "E_LIMIT_OPS");
  CHECK(p.getBlocks().empty());  // a capped block is not recorded, so nothing dumps
}

TEST_CASE("a chain MAX_TEMPLATE_DEPTH deep that does produce ops is never refused") {
  // The expansion count a legitimate script reaches is its ops times its nesting depth,
  // which is what MAX_TEMPLATE_EXPANSIONS is sized from: this one sits far inside it.
  std::string src = "@stencil t16:\n  @filter bw\n\n";
  for (int k = 15; k >= 1; --k)
    src += "@stencil t" + std::to_string(k) + ":\n  @use stencil t" + std::to_string(k + 1) +
           "\n\n";
  const ScriptProgram p = parse(src + repeat("@use stencil t1\n", 2000));
  CHECK_FALSE(p.hasErrors());
  CHECK(p.getOps().size() == 2000);
}

TEST_CASE("a script right at each cap is accepted") {
  // Exactly MAX_LINES lines: the last one carries no newline of its own.
  CHECK_FALSE(parse(repeat("\n", MAX_LINES - 1) + "@filter bw").hasErrors());
  CHECK_FALSE(parse(repeat("@filter bw\n", MAX_OPS)).hasErrors());
  CHECK_FALSE(parse(numbered("@source a", ".png:\n  @filter bw\n", MAX_BLOCKS)).hasErrors());
}
