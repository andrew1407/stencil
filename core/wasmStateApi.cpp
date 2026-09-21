// Handle-based WebAssembly ABI for core/state/: an opaque int from create/destroy,
// process-global, destroyed by the host. An unknown handle is a no-op returning a
// neutral value, never a crash. Driven op-for-op by browser/tests/wasm-parity*.

#include "HandleTable.hpp"
#include "HistoryStack.hpp"
#include "holdDraw.hpp"
#include "linesCodec.hpp"
#include <cstdint>

using namespace stencil::core;

namespace {

  // `result` holds the last undo/redo snapshot until the host reads it out.
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

  // Snapshots cross as the flat (nums, text) pair of abi/linesCodec.hpp, both ways.
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

  // Undo / redo: 1 with the buffer lengths in outSizes[0..1], 0 for the JS null. The
  // snapshot is read with stencil_history_readResult before the next call on the handle.
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
