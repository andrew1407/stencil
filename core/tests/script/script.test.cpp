#include "doctest.h"

#include "diagnostics.hpp"
#include "dump.hpp"
#include "lexer.hpp"
#include "scriptProgram.hpp"

#include <string>

// Mirrors browser/tests/script.test.js.
using namespace stencil::core::script;

namespace {

  ScriptProgram parse(const std::string& src) {
    return ScriptProgram::parse(src.data(), static_cast<int>(src.size()));
  }

  bool hasCode(const ScriptProgram& p, const char* code) {
    for (const Diagnostic& d : p.getDiagnostics())
      if (d.code == code) return true;
    return false;
  }

  std::vector<double> resolved(const ScriptProgram& p, int index, double w, double h) {
    double buf[64];
    const int n = resolveOp(p, index, w, h, 37.795275590551178, 37.795275590551178, buf, 64);
    return n > 0 ? std::vector<double>(buf, buf + n) : std::vector<double>();
  }

}  // namespace

TEST_CASE("lexer: '#' opens a comment unless the token is a hex colour") {
  const ScriptProgram p = parse("@source a.png:\n  @filter #ccc # tail\n");
  CHECK_FALSE(p.hasErrors());
  REQUIRE(p.getOps().size() == 2);
  CHECK(p.getOps()[1].strs[0] == "custom");
  CHECK(p.getOps()[1].strs[1] == "#ccc");
  CHECK(isHexColorWord("#aabbccdd"));
  CHECK_FALSE(isHexColorWord("#ggg"));
  CHECK_FALSE(isHexColorWord("#ccccc"));
}

TEST_CASE("lexer: a URL keeps its scheme and does not open a block") {
  const ScriptProgram p = parse("@source https://example.com/a.png:\n  @filter bw\n");
  CHECK_FALSE(p.hasErrors());
  REQUIRE(p.getBlocks().size() == 1);
  CHECK(p.getBlocks()[0].source == "https://example.com/a.png");
  CHECK(p.getBlocks()[0].kind == SourceKind::URL);
}

TEST_CASE("lexer: ';' separates statements like a newline") {
  const ScriptProgram p = parse("@crop 25%;@filter bw;@save out.png");
  CHECK_FALSE(p.hasErrors());
  CHECK(p.getOps().size() == 3);
}

TEST_CASE("directives and keywords ignore case; paths do not") {
  const ScriptProgram p = parse("@SOURCE A.png:\n  @FiLtEr BW\n");
  CHECK_FALSE(p.hasErrors());
  CHECK(p.getBlocks()[0].source == "A.png");
  CHECK(p.getOps()[1].strs[0] == "bw");
}

TEST_CASE("an unknown directive suggests the nearest one") {
  const ScriptProgram p = parse("@crp 10%\n");
  CHECK(hasCode(p, "E_UNKNOWN_DIRECTIVE"));
  CHECK(p.getDiagnostics()[0].message.find("@crop") != std::string::npos);
}

TEST_CASE("crop: the key form, and 1/2/4 positional insets") {
  const ScriptProgram keys = parse("@source a.png:\n  @crop x1=10% x2=-10% y1=2cm y2=-1in\n");
  CHECK_FALSE(keys.hasErrors());
  CHECK(keys.getOps()[1].toks[0] == "10%");
  CHECK(keys.getOps()[1].toks[3] == "-1in");

  const ScriptProgram one = parse("@source a.png:\n  @crop 10%\n");
  CHECK(one.getOps()[1].toks[0] == "10%");
  CHECK(one.getOps()[1].toks[1] == "-10%");
  CHECK(one.getOps()[1].toks[2] == "10%");
  CHECK(one.getOps()[1].toks[3] == "-10%");

  const ScriptProgram two = parse("@source a.png:\n  @crop 10% 20cm\n");
  CHECK(two.getOps()[1].toks[2] == "20cm");
  CHECK(two.getOps()[1].toks[3] == "-20cm");

  // Four values read x1 y1 x2 y2, so the second lands on the y axis.
  const ScriptProgram four = parse("@source a.png:\n  @crop 1% 2% 3% 4%\n");
  CHECK(four.getOps()[1].toks[0] == "1%");
  CHECK(four.getOps()[1].toks[1] == "3%");
  CHECK(four.getOps()[1].toks[2] == "2%");
  CHECK(four.getOps()[1].toks[3] == "4%");
}

TEST_CASE("crop: mixing the two forms is an error, as is a 3-value inset") {
  CHECK(hasCode(parse("@source a.png:\n  @crop x1=10% 20cm\n"), "E_CROP_MIXED_FORM"));
  CHECK(hasCode(parse("@source a.png:\n  @crop 1 2 3\n"), "E_CROP_ARITY"));
}

TEST_CASE("units: a bare number takes the current @use unit") {
  const ScriptProgram p = parse("@source a.png:\n  @use cm\n  @line (1,2) (3,4)\n");
  CHECK_FALSE(p.hasErrors());
  CHECK(p.getOps()[1].toks[0] == "1cm");
  CHECK(p.getOps()[1].toks[3] == "4cm");
}

TEST_CASE("points: a unit binds to a component or to the whole pair") {
  const ScriptProgram p = parse("@source a.png:\n  @line (14px, 50%) (56, 90)cm\n");
  CHECK_FALSE(p.hasErrors());
  const std::vector<std::string>& t = p.getOps()[1].toks;
  CHECK(t[0] == "14px");
  CHECK(t[1] == "50%");
  CHECK(t[2] == "56cm");
  CHECK(t[3] == "90cm");
}

TEST_CASE("@use line groups apply in any order") {
  const ScriptProgram a =
      parse("@source a.png:\n  @use line #cccccc dashed, fill aqua, point red 2px, 3px\n"
            "  @line (1,1) (2,2)\n");
  const ScriptProgram b =
      parse("@source a.png:\n  @use line 3px, point red 2px, dashed, fill aqua, #cccccc\n"
            "  @line (1,1) (2,2)\n");
  CHECK_FALSE(a.hasErrors());
  CHECK_FALSE(b.hasErrors());
  CHECK(a.getOps()[1].strs == b.getOps()[1].strs);
  CHECK(a.getOps()[1].nums == b.getOps()[1].nums);
  CHECK(a.getOps()[1].strs[0] == "#cccccc");
  CHECK(a.getOps()[1].strs[1] == "dashed");
  CHECK(a.getOps()[1].nums[0] == doctest::Approx(3.0));
}

TEST_CASE("two stroke colours in one @use line is an error") {
  CHECK(hasCode(parse("@source a.png:\n  @use line red blue\n"), "E_DUP_LINE_COLOR"));
}

TEST_CASE("a nested '@use Stencil' expands whatever its case") {
  const ScriptProgram p =
      parse("@stencil inner:\n  @filter bw\n\n@stencil outer:\n  @use Stencil inner\n\n"
            "@source a.png:\n  @use stencil outer\n");
  CHECK_FALSE(p.hasErrors());
  REQUIRE(p.getOps().size() == 2);
  CHECK(p.getOps()[1].kind == OpKind::FILTER);
}

TEST_CASE("a rect is a locked line; two corners become four points on resolve") {
  const ScriptProgram p = parse("@source a.png:\n  @rect (10,10) (100,80)\n");
  CHECK_FALSE(p.hasErrors());
  CHECK(p.getOps()[1].kind == OpKind::RECT);
  CHECK(p.getOps()[1].nums[2] == doctest::Approx(1.0));
  const std::vector<double> r = resolved(p, 1, 200, 100);
  REQUIRE(r.size() == 10);
  CHECK(r[0] == doctest::Approx(10));   // (10,10)
  CHECK(r[2] == doctest::Approx(100));  // (100,10)
  CHECK(r[3] == doctest::Approx(10));
  CHECK(r[5] == doctest::Approx(80));   // (100,80)
  CHECK(r[6] == doctest::Approx(10));   // (10,80)
}

TEST_CASE("a negative united length measures from the far edge") {
  const ScriptProgram p = parse("@source a.png:\n  @line (0,0) (-10%, -20%)\n");
  const std::vector<double> r = resolved(p, 1, 200, 100);
  REQUIRE(r.size() == 6);
  CHECK(r[2] == doctest::Approx(180));
  CHECK(r[3] == doctest::Approx(80));
}

TEST_CASE("crop resolves against the live image size") {
  const ScriptProgram p = parse("@source a.png:\n  @crop 10%\n");
  const std::vector<double> r = resolved(p, 1, 200, 100);
  REQUIRE(r.size() == 4);
  CHECK(r[0] == doctest::Approx(20));
  CHECK(r[2] == doctest::Approx(160));
  CHECK(r[3] == doctest::Approx(80));
}
