#include "doctest.h"
#include "hitTest.hpp"
#include "holdDraw.hpp"
#include <vector>

using namespace stencil::core;

// Mirrors browser/tests/holdDraw.test.js.

static Line lineOf(std::initializer_list<Point> pts) {
  Line l;
  l.points = std::vector<Point>(pts);
  return l;
}

TEST_CASE("holdDrawTarget: empty space → new line") {
  Lines empty;
  CHECK(holdDrawTarget(empty, 5, 5).kind == HoldTargetKind::NEW_LINE);
  Lines lines{lineOf({{0, 0}, {10, 0}})};
  CHECK(holdDrawTarget(lines, 200, 200).kind == HoldTargetKind::NEW_LINE);
}

TEST_CASE("holdDrawTarget: near a point → continue that line from it") {
  Lines lines{lineOf({{0, 0}, {100, 0}}), lineOf({{10, 10}, {50, 50}})};
  HoldTarget t = holdDrawTarget(lines, 11, 9);  // within 12px of lines[1].points[0]
  CHECK(t.kind == HoldTargetKind::CONTINUE_POINT);
  CHECK(t.lineIdx == 1);
  CHECK(t.ptIdx == 0);
}

TEST_CASE("holdDrawTarget: point beats segment when both are in range") {
  Lines lines{lineOf({{0, 0}, {100, 0}})};
  // (2,0) is on the segment AND within 12px of endpoint (0,0) → point wins.
  CHECK(holdDrawTarget(lines, 2, 0).kind == HoldTargetKind::CONTINUE_POINT);
}

TEST_CASE("holdDrawTarget: on a segment body (not a point) → insert") {
  Lines lines{lineOf({{0, 0}, {100, 0}})};
  HoldTarget t = holdDrawTarget(lines, 50, 3);  // mid-segment, far from endpoints
  CHECK(t.kind == HoldTargetKind::INSERT_SEGMENT);
  CHECK(t.lineIdx == 0);
  CHECK(t.ptIdx == 0);
  CHECK(t.ptIdx2 == 1);
}

TEST_CASE("holdDrawTarget: topmost (last) line wins for an overlapping point") {
  Lines lines{lineOf({{0, 0}, {10, 0}}), lineOf({{0, 0}, {10, 0}})};
  CHECK(holdDrawTarget(lines, 0, 0).lineIdx == 1);
}

TEST_CASE("controller: quick release before holdDelay never starts drawing") {
  HoldDrawController c(500.0);
  CHECK(c.pointerDown(10, 10, 0).action == HoldAction::ARMED);
  CHECK(c.state() == HoldState::ARMED);
  CHECK(c.tick(200).action == HoldAction::NONE);
  CHECK(c.pointerUp(300).action == HoldAction::NONE);  // released early → no commit
  CHECK(c.state() == HoldState::IDLE);
}

TEST_CASE("controller: moving past tolerance while armed aborts") {
  HoldDrawController c(500.0, 6.0);
  c.pointerDown(10, 10, 0);
  CHECK(c.pointerMove(13, 11, 50).action == HoldAction::NONE);    // within tolerance
  CHECK(c.pointerMove(30, 10, 100).action == HoldAction::ABORT);  // moved away
  CHECK(c.state() == HoldState::ABORTED);
  CHECK(c.tick(600).action == HoldAction::NONE);                  // no start after abort
  CHECK(c.pointerUp(700).action == HoldAction::NONE);
}

TEST_CASE("controller: hold fires start at the press point after holdDelay") {
  HoldDrawController c(500.0);
  c.pointerDown(20, 30, 0);
  CHECK(c.tick(400).action == HoldAction::NONE);
  HoldEvent s = c.tick(500);
  CHECK(s.action == HoldAction::START);
  CHECK(s.x == doctest::Approx(20));
  CHECK(s.y == doctest::Approx(30));
  CHECK(c.active());
}

TEST_CASE("controller: move emits preview while drawing") {
  HoldDrawController c(500.0);
  c.pointerDown(0, 0, 0);
  c.tick(500);  // start
  HoldEvent p = c.pointerMove(40, 0, 510);
  CHECK(p.action == HoldAction::PREVIEW);
  CHECK(p.x == doctest::Approx(40));
}

TEST_CASE("controller: dwell after moving away drops a point at the rest spot") {
  HoldDrawController c(500.0, 6.0, 10.0);
  c.pointerDown(0, 0, 0);
  c.tick(500);                // start → first point at (0,0)
  c.pointerMove(40, 0, 510);  // moved past rearm to a new rest spot
  CHECK(c.tick(900).action == HoldAction::NONE);  // only 390ms still
  HoldEvent d = c.tick(1010);                     // 500ms since rest at t=510
  CHECK(d.action == HoldAction::DROP);
  CHECK(d.x == doctest::Approx(40));
}

TEST_CASE("controller: no repeat drop without moving away (rearm gate)") {
  HoldDrawController c(500.0, 6.0, 10.0);
  c.pointerDown(0, 0, 0);
  c.tick(500);
  c.pointerMove(40, 0, 510);
  CHECK(c.tick(1010).action == HoldAction::DROP);  // first drop at (40,0)
  c.pointerMove(41, 0, 1100);  // within tolerance + rearm of the last drop
  CHECK(c.tick(2000).action == HoldAction::NONE);  // no second drop
}

TEST_CASE("controller: release after drawing commits; setHoldDelay + cancel work") {
  HoldDrawController c(500.0);
  c.pointerDown(0, 0, 0);
  c.tick(500);
  CHECK(c.pointerUp(800).action == HoldAction::COMMIT);
  CHECK(c.state() == HoldState::IDLE);

  c.setHoldDelay(200.0);
  CHECK(c.holdDelay() == doctest::Approx(200));
  c.pointerDown(0, 0, 0);
  CHECK(c.tick(200).action == HoldAction::START);
  c.cancel();
  CHECK(c.state() == HoldState::IDLE);
}
