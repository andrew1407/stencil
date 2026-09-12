// Native coverage for the handle-based WebAssembly ABI (core/wasmStateApi.cpp).
// emcc is not installed here, so the same translation unit is compiled natively
// (see CMakeLists) and driven through its extern "C" surface. Guards the handle
// lifetime and the action/state enum codes the browser wrapper decodes.
#include "doctest.h"

extern "C" {
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
static constexpr int kNone = 0, kArmed = 1, kAbort = 2, kStart = 3, kDrop = 4,
                     kPreview = 5, kCommit = 6;
// HoldState codes.
static constexpr int kIdle = 0, kStArmed = 1, kDrawing = 2, kAborted = 3;

TEST_CASE("holdDraw ABI: a hold runs armed → start → drop → commit") {
  double out[2] = {-1, -1};
  const int h = stencil_holdDraw_create(500, 6, 10);
  REQUIRE(h > 0);
  CHECK(stencil_holdDraw_state(h) == kIdle);
  CHECK(stencil_holdDraw_holdDelay(h) == 500);

  CHECK(stencil_holdDraw_pointerDown(h, 10, 10, 0, out) == kArmed);
  CHECK(stencil_holdDraw_state(h) == kStArmed);
  CHECK(stencil_holdDraw_tick(h, 100, out) == kNone);

  CHECK(stencil_holdDraw_tick(h, 500, out) == kStart);
  CHECK(out[0] == 10);
  CHECK(out[1] == 10);
  CHECK(stencil_holdDraw_state(h) == kDrawing);

  // Move past the re-arm distance, then dwell out the delay → a drop at the rest point.
  CHECK(stencil_holdDraw_pointerMove(h, 60, 10, 520, out) == kPreview);
  CHECK(out[0] == 60);
  CHECK(stencil_holdDraw_tick(h, 1100, out) == kDrop);
  CHECK(out[0] == 60);
  CHECK(out[1] == 10);

  CHECK(stencil_holdDraw_pointerUp(h, 1200, out) == kCommit);
  CHECK(stencil_holdDraw_state(h) == kIdle);
  stencil_holdDraw_destroy(h);
}

TEST_CASE("holdDraw ABI: moving past the tolerance before the hold aborts") {
  double out[2] = {0, 0};
  const int h = stencil_holdDraw_create(500, 6, 10);
  CHECK(stencil_holdDraw_pointerDown(h, 0, 0, 0, out) == kArmed);
  CHECK(stencil_holdDraw_pointerMove(h, 3, 3, 10, out) == kNone);   // inside tolerance
  CHECK(stencil_holdDraw_pointerMove(h, 40, 0, 20, out) == kAbort);
  CHECK(stencil_holdDraw_state(h) == kAborted);
  CHECK(stencil_holdDraw_tick(h, 9000, out) == kNone);
  CHECK(stencil_holdDraw_pointerUp(h, 9001, out) == kNone);
  stencil_holdDraw_destroy(h);
}

TEST_CASE("holdDraw ABI: handles are independent, and cancel resets one") {
  double out[2] = {0, 0};
  const int a = stencil_holdDraw_create(500, 6, 10);
  const int b = stencil_holdDraw_create(200, 6, 10);
  CHECK(a != b);
  stencil_holdDraw_pointerDown(a, 0, 0, 0, out);
  CHECK(stencil_holdDraw_state(b) == kIdle);
  CHECK(stencil_holdDraw_tick(b, 1000, out) == kNone);   // b was never pressed
  CHECK(stencil_holdDraw_holdDelay(b) == 200);

  stencil_holdDraw_setHoldDelay(b, -5);                  // negative is ignored
  CHECK(stencil_holdDraw_holdDelay(b) == 200);
  stencil_holdDraw_setHoldDelay(b, 50);
  CHECK(stencil_holdDraw_holdDelay(b) == 50);

  stencil_holdDraw_cancel(a);
  CHECK(stencil_holdDraw_state(a) == kIdle);
  stencil_holdDraw_destroy(a);
  stencil_holdDraw_destroy(b);
}

TEST_CASE("holdDraw ABI: an unknown or destroyed handle is inert, never a crash") {
  double out[2] = {7, 7};
  const int h = stencil_holdDraw_create(500, 6, 10);
  stencil_holdDraw_destroy(h);
  CHECK(stencil_holdDraw_state(h) == -1);
  CHECK(stencil_holdDraw_pointerDown(h, 1, 2, 3, out) == kNone);
  CHECK(stencil_holdDraw_tick(h, 9, out) == kNone);
  CHECK(stencil_holdDraw_pointerUp(h, 9, out) == kNone);
  CHECK(stencil_holdDraw_holdDelay(0) == 0);
  stencil_holdDraw_cancel(-1);                            // no-op
  stencil_holdDraw_destroy(h);                            // double destroy is a no-op
  CHECK(out[0] == 7);                                     // out is left untouched
}
