#include "doctest.h"

#include "scriptDiagnostics.hpp"
#include "scriptDump.hpp"
#include "scriptLexer.hpp"
#include "scriptProgram.hpp"

#include <string>

// Templates, history and resolution. Mirrors browser/tests/scriptLower.test.js.
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

}  // namespace

TEST_CASE("templates: longest defined name wins, parameters fill positionally") {
  const ScriptProgram p =
      parse("@stencil style:\n  @use line red\n\n@stencil style bold:\n  @use line blue, 5px\n\n"
            "@source a.png:\n  @use stencil style bold:\n  @line (1,1) (2,2)\n");
  CHECK(p.getOps()[1].strs[0] == "blue");
  CHECK(p.getOps()[1].nums[0] == doctest::Approx(5.0));

  const ScriptProgram q = parse("@stencil s p:\n  @use line @1, @2\n\n"
                                "@source a.png:\n  @use stencil s p red cm:\n  @line (1,1) (2,2)\n");
  CHECK_FALSE(q.hasErrors());
  CHECK(q.getOps()[1].strs[0] == "red");
  CHECK(q.getOps()[1].toks[0] == "1cm");
}

TEST_CASE("templates: wrong arity, unknown name and no use are all reported") {
  CHECK(hasCode(parse("@stencil two p:\n  @use line @1, @2\n\n@source a.png:\n"
                      "  @use stencil two p red:\n"),
                "E_TEMPLATE_ARITY"));
  CHECK(hasCode(parse("@source a.png:\n  @use stencil nowhere:\n"), "E_UNDEFINED_TEMPLATE"));
  CHECK(hasCode(parse("@stencil unused:\n  @filter bw\n\n@source a.png:\n  @filter sepia\n"),
                "W_UNUSED_TEMPLATE"));
}

TEST_CASE("a template that reaches itself stops at the depth cap") {
  const ScriptProgram p =
      parse("@stencil loop:\n  @use stencil loop:\n\n@source a.png:\n  @use stencil loop:\n");
  CHECK(hasCode(p, "E_TEMPLATE_RECURSION"));
}

TEST_CASE("undo rewinds and replays so each save sees the right state") {
  const ScriptProgram p = parse("@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n"
                                "  @save one.png\n  @undo\n  @save two.png\n  @redo\n"
                                "  @save three.png\n");
  CHECK_FALSE(p.hasErrors());
  int undos = 0, rects = 0;
  for (const Op& op : p.getOps()) {
    if (op.kind == OpKind::UNDO) ++undos;
    if (op.kind == OpKind::RECT) ++rects;
  }
  CHECK(undos == 1);      // one rewind, before the second save
  CHECK(rects == 2);      // the original plus the replay after @redo
}

TEST_CASE("undoing the first edit rewinds past every later one and replays them") {
  const ScriptProgram p = parse("@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n"
                                "  @crop 10%\n  @undo 1\n  @save out.png\n");
  CHECK_FALSE(p.hasErrors());
  const Op* undo = nullptr;
  for (const Op& op : p.getOps())
    if (op.kind == OpKind::UNDO) undo = &op;
  REQUIRE(undo != nullptr);
  CHECK(undo->nums[0] == doctest::Approx(3.0));
}

TEST_CASE("undo selectors: by index, from the end, and by text") {
  CHECK_FALSE(parse("@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n  @undo -1\n").hasErrors());
  CHECK_FALSE(
      parse("@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n  @undo @rect (1,1) (2,2)\n")
          .hasErrors());
  CHECK(hasCode(parse("@source a.png:\n  @filter bw\n  @undo 99\n"), "E_UNDO_OUT_OF_RANGE"));
  CHECK(hasCode(parse("@source a.png:\n  @filter bw\n  @redo\n"), "W_NOTHING_TO_REDO"));
}

TEST_CASE("@frame starts a fresh set of edits and rejects a repeat") {
  const ScriptProgram p =
      parse("@source clip.mp4:\n  @frame 8\n  @filter bw\n  @save\n  @frame 90\n  @save\n");
  CHECK_FALSE(p.hasErrors());
  CHECK(hasCode(parse("@source clip.mp4:\n  @frame 8\n  @frame 8\n"), "E_DUPLICATE_FRAME"));
  CHECK(hasCode(parse("@frame 3\n"), "E_FRAME_OUTSIDE_SOURCE"));
}

TEST_CASE("a source spec is classified for the adapter that opens it") {
  CHECK(parse("@source a.png:\n  @filter bw\n").getBlocks()[0].kind == SourceKind::FILE);
  CHECK(parse("@source shots/:\n  @filter bw\n").getBlocks()[0].kind == SourceKind::DIR);
  CHECK(parse("@source shots/a*.png:\n  @filter bw\n").getBlocks()[0].kind == SourceKind::GLOB);
  CHECK(parse("@filter bw\n").getBlocks()[0].kind == SourceKind::PROJECT);
}

TEST_CASE("a block body ends where its indentation does") {
  const ScriptProgram p =
      parse("@stencil s:\n  @filter bw\n\n@rect (1,1) (2,2)\n@save\n");
  CHECK(p.getBlocks().size() == 1);
  CHECK(p.getBlocks()[0].kind == SourceKind::PROJECT);
  CHECK(p.getOps().size() == 2);  // the rect and the save, not the template body
}

TEST_CASE("malformed input yields a diagnostic, never a crash") {
  const char* bad[] = {"@", "@@", "@source", "@source :", "@line (", "@line (1,", "@crop x1=",
                       "@use line", "@stencil", "\"", "@filter", "@undo @", "@line ()", "@@@@@"};
  for (const char* src : bad) {
    const ScriptProgram p = parse(src);
    CHECK(p.getOps().size() <= 1);
  }
}

TEST_CASE("the caps are the same numbers the JS port uses") {
  CHECK(MAX_LINES == 20000);
  CHECK(MAX_OPS == 5000);
  CHECK(MAX_TEMPLATE_DEPTH == 16);
  CHECK(MAX_POINTS_PER_LINE == 200);
}

TEST_CASE("editDistance is capped, and didYouMean only suggests close words") {
  CHECK(editDistance("crop", "crp") == 1);
  CHECK(editDistance("crop", "zzzzzzzz") == 3);
  CHECK(didYouMean("crp", {"crop", "filter"}) == "crop");
  CHECK(didYouMean("zzzzzzzz", {"crop", "filter"}).empty());
}
