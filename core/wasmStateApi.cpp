// Handle-based WebAssembly ABI for core/state/'s hold-draw machine: an opaque int from
// create/destroy, process-global, destroyed by the host. An unknown handle is a no-op returning
// a neutral value, never a crash. The history's handles are wasmHistoryApi.cpp.

#include "HandleTable.hpp"
#include "holdDraw.hpp"
#include <cstdint>

using namespace stencil::core;

namespace {

  abi::HandleTable<HoldDrawController>& holdDraws() {
    static abi::HandleTable<HoldDrawController> table;
    return table;
  }

  int emit(const HoldEvent& ev, double* out) {
    if (out != nullptr) {
      out[0] = ev.x;
      out[1] = ev.y;
    }
    return static_cast<int>(ev.action);
  }

}

extern "C" {

  // Times are monotonic ms, coordinates host screen space.
  int stencil_holdDraw_create(double holdDelay, double moveTolerance,
                              double rearmDistance) {
    return holdDraws().create(holdDelay, moveTolerance, rearmDistance);
  }

  void stencil_holdDraw_destroy(int handle) { holdDraws().destroy(handle); }

  // HoldState code (0 Idle, 1 Armed, 2 Drawing, 3 Aborted), -1 for an unknown handle.
  int stencil_holdDraw_state(int handle) {
    const HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? -1 : static_cast<int>(c->getState());
  }

  double stencil_holdDraw_holdDelay(int handle) {
    const HoldDrawController* c = holdDraws().get(handle);
    return c == nullptr ? 0.0 : c->getHoldDelay();
  }

  void stencil_holdDraw_setHoldDelay(int handle, double ms) {
    HoldDrawController* c = holdDraws().get(handle);
    if (c != nullptr) c->setHoldDelay(ms);
  }

  void stencil_holdDraw_cancel(int handle) {
    HoldDrawController* c = holdDraws().get(handle);
    if (c != nullptr) c->cancel();
  }

  // The four drivers return a HoldAction code (0 None … 6 Commit), coords in out[0..1].
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
