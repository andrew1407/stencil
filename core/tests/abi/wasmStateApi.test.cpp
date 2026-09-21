// Native coverage for the handle-based WebAssembly ABI (core/wasmStateApi.cpp).
// emcc is not installed here, so the same translation unit is compiled natively
// (see CMakeLists) and driven through its extern "C" surface. Guards the handle
// lifetime and the action/state enum codes the browser wrapper decodes.
#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
  int stencil_history_create(void);
  void stencil_history_destroy(int);
  void stencil_history_reset(int, int, int, const double*, int, const std::uint8_t*, int);
  void stencil_history_push(int, const double*, int, const std::uint8_t*, int);
  int stencil_history_canUndo(int);
  int stencil_history_canRedo(int);
  int stencil_history_step(int);
  int stencil_history_size(int);
  int stencil_history_undo(int, int*);
  int stencil_history_redo(int, int*);
  void stencil_history_readResult(int, double*, std::uint8_t*);
  int stencil_holdDraw_create(double, double, double);
  void stencil_holdDraw_destroy(int);
  int stencil_holdDraw_state(int);
  double stencil_holdDraw_holdDelay(int);
  void stencil_holdDraw_setHoldDelay(int, double);
  void stencil_holdDraw_cancel(int);
  int stencil_holdDraw_pointerDown(int, double, double, double, double*);
  int stencil_holdDraw_pointerMove(int, double, double, double, double*);
  int stencil_holdDraw_tick(int, double, double*);
  int stencil_holdDraw_pointerUp(int, double, double*);
}

// HoldAction codes (holdDraw.hpp order).
static constexpr int HOLD_NONE = 0, ARMED = 1, ABORT = 2, START = 3, DROP = 4,
                     PREVIEW = 5, COMMIT = 6;
// HoldState codes.
static constexpr int IDLE = 0, ST_ARMED = 1, DRAWING = 2, ABORTED = 3;

TEST_CASE("holdDraw ABI: a hold runs armed → start → drop → commit") {
  double out[2] = {-1, -1};
  const int h = stencil_holdDraw_create(500, 6, 10);
  REQUIRE(h > 0);
  CHECK(stencil_holdDraw_state(h) == IDLE);
  CHECK(stencil_holdDraw_holdDelay(h) == 500);

  CHECK(stencil_holdDraw_pointerDown(h, 10, 10, 0, out) == ARMED);
  CHECK(stencil_holdDraw_state(h) == ST_ARMED);
  CHECK(stencil_holdDraw_tick(h, 100, out) == HOLD_NONE);

  CHECK(stencil_holdDraw_tick(h, 500, out) == START);
  CHECK(out[0] == 10);
  CHECK(out[1] == 10);
  CHECK(stencil_holdDraw_state(h) == DRAWING);

  // Move past the re-arm distance, then dwell out the delay → a drop at the rest point.
  CHECK(stencil_holdDraw_pointerMove(h, 60, 10, 520, out) == PREVIEW);
  CHECK(out[0] == 60);
  CHECK(stencil_holdDraw_tick(h, 1100, out) == DROP);
  CHECK(out[0] == 60);
  CHECK(out[1] == 10);

  CHECK(stencil_holdDraw_pointerUp(h, 1200, out) == COMMIT);
  CHECK(stencil_holdDraw_state(h) == IDLE);
  stencil_holdDraw_destroy(h);
}

TEST_CASE("holdDraw ABI: moving past the tolerance before the hold aborts") {
  double out[2] = {0, 0};
  const int h = stencil_holdDraw_create(500, 6, 10);
  CHECK(stencil_holdDraw_pointerDown(h, 0, 0, 0, out) == ARMED);
  CHECK(stencil_holdDraw_pointerMove(h, 3, 3, 10, out) == HOLD_NONE);   // inside tolerance
  CHECK(stencil_holdDraw_pointerMove(h, 40, 0, 20, out) == ABORT);
  CHECK(stencil_holdDraw_state(h) == ABORTED);
  CHECK(stencil_holdDraw_tick(h, 9000, out) == HOLD_NONE);
  CHECK(stencil_holdDraw_pointerUp(h, 9001, out) == HOLD_NONE);
  stencil_holdDraw_destroy(h);
}

TEST_CASE("holdDraw ABI: handles are independent, and cancel resets one") {
  double out[2] = {0, 0};
  const int a = stencil_holdDraw_create(500, 6, 10);
  const int b = stencil_holdDraw_create(200, 6, 10);
  CHECK(a != b);
  stencil_holdDraw_pointerDown(a, 0, 0, 0, out);
  CHECK(stencil_holdDraw_state(b) == IDLE);
  CHECK(stencil_holdDraw_tick(b, 1000, out) == HOLD_NONE);   // b was never pressed
  CHECK(stencil_holdDraw_holdDelay(b) == 200);

  stencil_holdDraw_setHoldDelay(b, -5);                  // negative is ignored
  CHECK(stencil_holdDraw_holdDelay(b) == 200);
  stencil_holdDraw_setHoldDelay(b, 50);
  CHECK(stencil_holdDraw_holdDelay(b) == 50);

  stencil_holdDraw_cancel(a);
  CHECK(stencil_holdDraw_state(a) == IDLE);
  stencil_holdDraw_destroy(a);
  stencil_holdDraw_destroy(b);
}

TEST_CASE("holdDraw ABI: an unknown or destroyed handle is inert, never a crash") {
  double out[2] = {7, 7};
  const int h = stencil_holdDraw_create(500, 6, 10);
  stencil_holdDraw_destroy(h);
  CHECK(stencil_holdDraw_state(h) == -1);
  CHECK(stencil_holdDraw_pointerDown(h, 1, 2, 3, out) == HOLD_NONE);
  CHECK(stencil_holdDraw_tick(h, 9, out) == HOLD_NONE);
  CHECK(stencil_holdDraw_pointerUp(h, 9, out) == HOLD_NONE);
  CHECK(stencil_holdDraw_holdDelay(0) == 0);
  stencil_holdDraw_cancel(-1);                            // no-op
  stencil_holdDraw_destroy(h);                            // double destroy is a no-op
  CHECK(out[0] == 7);                                     // out is left untouched
}


// ── history: snapshots cross as the flat (nums, text) pair ──
#include "linesCodec.hpp"

using namespace stencil::core;

// Encode `lines` the way the browser wrapper does, then push it through the ABI.
static void pushLines(int h, const Lines& lines) {
  const abi::LinesSize sz = abi::linesSize(lines);
  std::vector<double> nums(static_cast<std::size_t>(sz.nums));
  std::vector<std::uint8_t> text(static_cast<std::size_t>(sz.text) + 1);
  abi::encodeLines(lines, nums.data(), text.data());
  stencil_history_push(h, nums.data(), sz.nums, text.data(), sz.text);
}

// Read back whatever the last undo/redo left, given its reported sizes.
static Lines readResult(int h, const int* sizes) {
  std::vector<double> nums(static_cast<std::size_t>(sizes[0]));
  std::vector<std::uint8_t> text(static_cast<std::size_t>(sizes[1]) + 1);
  stencil_history_readResult(h, nums.data(), text.data());
  return abi::decodeLines(nums.data(), sizes[0], text.data(), sizes[1]);
}

static Lines snapshot(const char* color, std::initializer_list<Point> pts) {
  Line l;
  l.color = color;  l.style = "dashed";
  l.fillColor = "";  l.pointColor = "#0f0";
  l.thickness = 3.5;  l.locked = true;
  l.points = std::vector<Point>(pts);
  return Lines{l};
}

TEST_CASE("history ABI: a snapshot survives the round trip field for field") {
  int sizes[2] = {0, 0};
  const int h = stencil_history_create();
  const Lines a = snapshot("#abcdef", {{1.5, -2.5}, {30, 40}});
  pushLines(h, a);
  pushLines(h, snapshot("#123456", {{7, 8}}));
  CHECK(stencil_history_size(h) == 2);
  CHECK(stencil_history_step(h) == 1);

  REQUIRE(stencil_history_undo(h, sizes) == 1);
  const Lines back = readResult(h, sizes);
  REQUIRE(back.size() == 1);
  CHECK(back[0].color == a[0].color);
  CHECK(back[0].style == a[0].style);
  CHECK(back[0].fillColor.empty());
  CHECK(back[0].pointColor == a[0].pointColor);
  CHECK(back[0].thickness == a[0].thickness);
  CHECK(back[0].locked);
  REQUIRE(back[0].points.size() == 2);
  CHECK(back[0].points[1].y == 40);
  stencil_history_destroy(h);
}

TEST_CASE("history ABI: cursor semantics, including undo at step 0") {
  int sizes[2] = {0, 0};
  const int h = stencil_history_create();
  CHECK(stencil_history_step(h) == -1);
  CHECK(stencil_history_canUndo(h) == 0);
  pushLines(h, snapshot("#fff", {{0, 0}}));
  CHECK(stencil_history_canUndo(h) == 1);
  CHECK(stencil_history_canRedo(h) == 0);

  // Step 0 undoes to an EMPTY snapshot and moves to -1 — the pinned quirk.
  REQUIRE(stencil_history_undo(h, sizes) == 1);
  CHECK(readResult(h, sizes).empty());
  CHECK(stencil_history_step(h) == -1);
  CHECK(stencil_history_undo(h, sizes) == 0);
  REQUIRE(stencil_history_redo(h, sizes) == 1);
  CHECK(readResult(h, sizes).size() == 1);
  stencil_history_destroy(h);
}

TEST_CASE("history ABI: reset honours the default and explicit base step") {
  const int h = stencil_history_create();
  const Lines one = snapshot("#fff", {{0, 0}});
  const abi::LinesSize sz = abi::linesSize(one);
  std::vector<double> nums(static_cast<std::size_t>(sz.nums));
  std::vector<std::uint8_t> text(static_cast<std::size_t>(sz.text) + 1);
  abi::encodeLines(one, nums.data(), text.data());

  stencil_history_reset(h, 0, 0, nums.data(), sz.nums, text.data(), sz.text);
  CHECK(stencil_history_step(h) == 0);          // default: lines present → 0
  stencil_history_reset(h, 0, 0, nullptr, 0, nullptr, 0);
  CHECK(stencil_history_step(h) == -1);         // default: no lines → -1
  CHECK(stencil_history_size(h) == 0);
  stencil_history_reset(h, 1, 0, nums.data(), sz.nums, text.data(), sz.text);
  CHECK(stencil_history_step(h) == 0);
  CHECK(stencil_history_canRedo(h) == 0);
  stencil_history_destroy(h);
}

TEST_CASE("history ABI: a truncated snapshot buffer decodes to the complete lines only") {
  int sizes[2] = {0, 0};
  const int h = stencil_history_create();
  Lines two = snapshot("#fff", {{0, 0}});
  two.push_back(two[0]);
  const abi::LinesSize sz = abi::linesSize(two);
  std::vector<double> nums(static_cast<std::size_t>(sz.nums));
  std::vector<std::uint8_t> text(static_cast<std::size_t>(sz.text) + 1);
  abi::encodeLines(two, nums.data(), text.data());

  pushLines(h, two);
  // Claim two lines but hand over one line's worth of numbers.
  stencil_history_push(h, nums.data(), sz.nums - 4, text.data(), sz.text);
  REQUIRE(stencil_history_undo(h, sizes) == 1);
  CHECK(readResult(h, sizes).size() == 2);      // the intact snapshot
  REQUIRE(stencil_history_redo(h, sizes) == 1);
  CHECK(readResult(h, sizes).size() == 1);      // the truncated one, minus its stub
  CHECK(stencil_history_step(0) == -1);         // unknown handle stays inert
  stencil_history_destroy(h);
}
