// Handle-based WebAssembly ABI for the core's STATEFUL classes.
//
// wasmApi.cpp exports pure functions; the classes in core/state/ own state, so
// they cross the boundary as an opaque int handle from a create/destroy pair plus
// operations that take it. Nothing but ints and doubles crosses, so the browser's
// JS twin can be driven op-for-op against the C++ one (browser/tests/wasm-parity*).
//
// Handles are process-global and the host must destroy what it creates. An unknown
// handle is a no-op returning a neutral value, never a crash.

#include "handleTable.hpp"
#include "holdDraw.hpp"

using namespace stencil::core;

namespace {

  abi::HandleTable<HoldDrawController>& holdDraws() {
    static abi::HandleTable<HoldDrawController> table;
    return table;
  }

  // Write the event's coordinates to out[0..1] and return its HoldAction code.
  int emit(const HoldEvent& ev, double* out) {
    if (out != nullptr) {
      out[0] = ev.x;
      out[1] = ev.y;
    }
    return static_cast<int>(ev.action);
  }

}

extern "C" {

  // ── hold-to-draw gesture machine (browser/js/core/holdDraw.js) ──
  // Returns a handle > 0. Times are monotonic ms, coordinates host screen space.
  int stencil_holdDraw_create(double holdDelay, double moveTolerance,
                              double rearmDistance) {
    return holdDraws().create(holdDelay, moveTolerance, rearmDistance);
  }

  void stencil_holdDraw_destroy(int handle) { holdDraws().destroy(handle); }

  // HoldState code (0 Idle, 1 Armed, 2 Drawing, 3 Aborted), -1 for an unknown handle.
  int stencil_holdDraw_state(int handle) {
    const HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? -1 : static_cast<int>(c->state());
  }

  double stencil_holdDraw_holdDelay(int handle) {
    const HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? 0.0 : c->holdDelay();
  }

  void stencil_holdDraw_setHoldDelay(int handle, double ms) {
    HoldDrawController* c = holdDraws().get(handle);
    if (c != nullptr) c->setHoldDelay(ms);
  }

  void stencil_holdDraw_cancel(int handle) {
    HoldDrawController* c = holdDraws().get(handle);
    if (c != nullptr) c->cancel();
  }

  // The four drivers all return a HoldAction code (0 None … 6 Commit) and write the
  // action's coordinates to out[0..1]; an unknown handle yields None.
  int stencil_holdDraw_pointerDown(int handle, double x, double y, double t,
                                   double* out) {
    HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? 0 : emit(c->pointerDown(x, y, t), out);
  }

  int stencil_holdDraw_pointerMove(int handle, double x, double y, double t,
                                   double* out) {
    HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? 0 : emit(c->pointerMove(x, y, t), out);
  }

  int stencil_holdDraw_tick(int handle, double t, double* out) {
    HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? 0 : emit(c->tick(t), out);
  }

  int stencil_holdDraw_pointerUp(int handle, double t, double* out) {
    HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? 0 : emit(c->pointerUp(t), out);
  }

}
