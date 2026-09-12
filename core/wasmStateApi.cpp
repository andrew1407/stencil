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
#include "historyStack.hpp"
#include "holdDraw.hpp"
#include "linesCodec.hpp"
#include <cstdint>

using namespace stencil::core;

namespace {

  // A stack plus the snapshot its last undo/redo produced: the host reads the result
  // out in a second call, once it knows how big the two buffers have to be.
  struct HistorySlot {
    HistoryStack stack;
    Lines result;
  };

  abi::HandleTable<HistorySlot>& histories() {
    static abi::HandleTable<HistorySlot> table;
    return table;
  }

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

  // ── line-snapshot history (browser/js/core/historyStack.js) ──
  // Snapshots cross as the flat (nums, text) pair from abi/linesCodec.hpp, both
  // directions. Getters answer 0 / -1 for an unknown handle.
  int stencil_history_create(void) { return histories().create(); }

  void stencil_history_destroy(int handle) { histories().destroy(handle); }

  // hasStep 0 takes the JS default base step (0 when there are lines, else -1).
  void stencil_history_reset(int handle, int hasStep, int baseStep, const double* nums,
                             int numsLen, const std::uint8_t* text, int textLen) {
    HistorySlot* h = histories().get(handle);
    if (h == nullptr) return;
    const Lines lines = abi::decodeLines(nums, numsLen, text, textLen);
    if (hasStep != 0) h->stack.reset(lines, baseStep);
    else h->stack.reset(lines);
  }

  void stencil_history_push(int handle, const double* nums, int numsLen,
                            const std::uint8_t* text, int textLen) {
    HistorySlot* h = histories().get(handle);
    if (h != nullptr) h->stack.push(abi::decodeLines(nums, numsLen, text, textLen));
  }

  int stencil_history_canUndo(int handle) {
    const HistorySlot* h = histories().get(handle);
    return h != nullptr && h->stack.canUndo() ? 1 : 0;
  }

  int stencil_history_canRedo(int handle) {
    const HistorySlot* h = histories().get(handle);
    return h != nullptr && h->stack.canRedo() ? 1 : 0;
  }

  int stencil_history_step(int handle) {
    const HistorySlot* h = histories().get(handle);
    return h == nullptr ? -1 : h->stack.step();
  }

  int stencil_history_size(int handle) {
    const HistorySlot* h = histories().get(handle);
    return h == nullptr ? 0 : static_cast<int>(h->stack.size());
  }

  // Undo / redo: 1 when a snapshot is ready and its buffer lengths are written to
  // outSizes[0..1], 0 for the JS null (nothing to undo / redo). The snapshot itself
  // is read with stencil_history_readResult before the next call on this handle.
  int stencil_history_undo(int handle, int* outSizes) {
    HistorySlot* h = histories().get(handle);
    if (h == nullptr) return 0;
    std::optional<Lines> r = h->stack.undo();
    if (!r.has_value()) return 0;
    h->result = std::move(*r);
    const abi::LinesSize s = abi::linesSize(h->result);
    if (outSizes != nullptr) { outSizes[0] = s.nums; outSizes[1] = s.text; }
    return 1;
  }

  int stencil_history_redo(int handle, int* outSizes) {
    HistorySlot* h = histories().get(handle);
    if (h == nullptr) return 0;
    std::optional<Lines> r = h->stack.redo();
    if (!r.has_value()) return 0;
    h->result = std::move(*r);
    const abi::LinesSize s = abi::linesSize(h->result);
    if (outSizes != nullptr) { outSizes[0] = s.nums; outSizes[1] = s.text; }
    return 1;
  }

  void stencil_history_readResult(int handle, double* nums, std::uint8_t* text) {
    const HistorySlot* h = histories().get(handle);
    if (h != nullptr) abi::encodeLines(h->result, nums, text);
  }

}
