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

TEST_CASE("push caps the depth at kMaxSteps, evicting the oldest snapshot") {
  HistoryStack h;
  const int cap = static_cast<int>(HistoryStack::kMaxSteps);
  CHECK(cap == 64);  // LIMITS.historyMax in browser/js/config/constants.json
  for (int i = 0; i < cap; ++i) h.push({lineWithX(i)});
  CHECK(h.size() == HistoryStack::kMaxSteps);
  CHECK(h.step() == cap - 1);

  // One past the cap: size holds, the cursor still names the snapshot just pushed,
  // and the oldest (x == 0) is gone — the deepest undo now reaches x == 1.
  h.push({lineWithX(cap)});
  CHECK(h.size() == HistoryStack::kMaxSteps);
  CHECK(h.step() == cap - 1);
  CHECK_FALSE(h.canRedo());
  for (int i = 0; i < cap - 1; ++i) h.undo();
  CHECK(h.step() == 0);
  const auto deepest = h.undo();
  REQUIRE(deepest.has_value());
  CHECK(deepest->empty());  // past the front is still the "empty lines, step -1" stop
  CHECK(h.step() == -1);
  const auto back = h.redo();
  REQUIRE(back.has_value());
  CHECK((*back)[0].points[0].x == doctest::Approx(1.0));
}

TEST_CASE("the cap holds over a long run and leaves redo reachable") {
  HistoryStack h;
  const int cap = static_cast<int>(HistoryStack::kMaxSteps);
  for (int i = 0; i < cap * 3; ++i) {
    h.push({lineWithX(i)});
    CHECK(h.size() <= HistoryStack::kMaxSteps);
    CHECK(h.step() == static_cast<int>(h.size()) - 1);
  }
  h.undo();
  CHECK(h.canRedo());
  const auto r = h.redo();
  REQUIRE(r.has_value());
  CHECK((*r)[0].points[0].x == doctest::Approx(cap * 3 - 1));
}
