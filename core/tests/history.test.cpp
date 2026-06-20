#include "doctest.h"
#include "historyStack.hpp"

using namespace stencil::core;

// Mirrors browser/tests/history.test.js.

static Line lineWithX(double x) {
  Line l;
  l.points.push_back(Point{x, 0});
  return l;
}

TEST_CASE("fresh stack cannot undo at base") {
  HistoryStack h;
  CHECK_FALSE(h.canUndo());
  CHECK_FALSE(h.canRedo());
}

TEST_CASE("push copies the snapshot (mutating original does not change stored)") {
  HistoryStack h;
  Lines lines = {lineWithX(1)};
  h.push(lines);  // step 0
  h.push(lines);  // step 1
  lines[0].points[0].x = 999;
  const auto restored = h.undo();  // back to step 0
  REQUIRE(restored.has_value());
  CHECK((*restored)[0].points[0].x == doctest::Approx(1.0));
}

TEST_CASE("undo returns prior snapshot; redo returns next; canRedo false at top") {
  HistoryStack h;
  h.push({lineWithX(1)});  // step 0 (a)
  h.push({lineWithX(2)});  // step 1 (b)
  CHECK_FALSE(h.canRedo());
  const auto u = h.undo();  // step 0
  REQUIRE(u.has_value());
  CHECK((*u)[0].points[0].x == doctest::Approx(1.0));
  CHECK(h.canRedo());
  const auto r = h.redo();  // step 1
  REQUIRE(r.has_value());
  CHECK((*r)[0].points[0].x == doctest::Approx(2.0));
  CHECK_FALSE(h.canRedo());
}

TEST_CASE("push after undo truncates the redo branch") {
  HistoryStack h;
  h.push({lineWithX(1)});  // 0
  h.push({lineWithX(2)});  // 1
  h.push({lineWithX(3)});  // 2
  h.undo();                // 1
  h.undo();                // 0
  h.push({lineWithX(4)});  // 1, truncating the old branch
  CHECK_FALSE(h.canRedo());
  CHECK(h.step() == 1);
}

TEST_CASE("step-to-empty semantics: undo at step 0 -> empty lines, step -1") {
  HistoryStack h;
  h.push({lineWithX(1)});           // step 0
  const auto u = h.undo();          // step 0 -> [], step -1
  REQUIRE(u.has_value());
  CHECK(u->empty());
  CHECK(h.step() == -1);
  CHECK_FALSE(h.undo().has_value());  // nothing further
}

TEST_CASE("reset initializes from base lines") {
  HistoryStack h;
  h.reset({lineWithX(0)});  // non-empty -> step 0
  CHECK(h.step() == 0);
  CHECK(h.canUndo());
  h.reset({});              // empty -> step -1
  CHECK(h.step() == -1);
  CHECK_FALSE(h.canUndo());
}

TEST_CASE("reset with empty lines leaves NO redo (no stray redo step after a blank)") {
  HistoryStack h;
  h.push({lineWithX(0)});  // simulate prior edits
  h.reset({});             // e.g. creating a blank image / fresh load
  CHECK_FALSE(h.canUndo());
  CHECK_FALSE(h.canRedo());  // was true: phantom empty snapshot
  CHECK_FALSE(h.redo().has_value());
  // A real edit after reset still undoes back to empty and redoes forward.
  h.push({lineWithX(1)});
  CHECK(h.canUndo());
  auto u = h.undo();
  CHECK(u.has_value());
  CHECK(u->empty());
  CHECK(h.canRedo());
  auto r = h.redo();
  CHECK(r.has_value());
  CHECK(r->size() == 1);
}
