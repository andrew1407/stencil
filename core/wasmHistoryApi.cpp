// Handle-based WebAssembly ABI for the editor's undo history (state/HistoryStack): one
// EditorHistory per handle. Lines cross as the abi/linesCodec.hpp pair; a memento's view as
// [x, y, width, height, quarters, mirrored]; a lines-only step passes no view. Unknown handles no-op.

#include "HandleTable.hpp"
#include "HistoryStack.hpp"
#include "linesCodec.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

using namespace stencil::core;

namespace {

  // `result` holds the last undo/redo step until the host reads it out.
  struct HistorySlot {
    EditorHistory stack;
    EditorMemento result;
  };

  abi::HandleTable<HistorySlot>& histories() {
    static abi::HandleTable<HistorySlot> table;
    return table;
  }

  EditorMemento mementoOf(const double* nums, int numsLen, const std::uint8_t* text, int textLen,
                          const double* view, const char* filter, const char* filterColor) {
    EditorMemento m;
    m.lines = abi::decodeLines(nums, numsLen, text, textLen);
    if (view != nullptr) {
      m.hasView = true;
      m.crop = CropRect{view[0], view[1], view[2], view[3]};
      // A count past int range (or NaN) would make the cast undefined.
      m.quarters = std::abs(view[4]) < 1e9 ? static_cast<int>(view[4]) : 0;
      m.mirrored = view[5] != 0.0;
      m.filter = filter != nullptr ? filter : "";
      m.filterColor = filterColor != nullptr ? filterColor : "";
    }
    return m;
  }

  // 1 with the buffer lengths in outSizes[0..1], 0 for the JS null.
  int retain(HistorySlot* h, std::optional<EditorMemento> r, int* outSizes) {
    if (!r.has_value()) return 0;
    h->result = std::move(*r);
    const abi::LinesSize s = abi::linesSize(h->result.lines);
    if (outSizes != nullptr) { outSizes[0] = s.nums; outSizes[1] = s.text; }
    return 1;
  }

}

extern "C" {

  int stencil_history_create(void) { return histories().create(); }

  void stencil_history_destroy(int handle) { histories().destroy(handle); }

  // hasStep 0 takes the JS default base step (0 when there are lines, else -1).
  void stencil_history_resetMemento(int handle, int hasStep, int baseStep, const double* nums,
                                    int numsLen, const std::uint8_t* text, int textLen,
                                    const double* view, const char* filter,
                                    const char* filterColor) {
    HistorySlot* h = histories().get(handle);
    if (h == nullptr) return;
    const EditorMemento m = mementoOf(nums, numsLen, text, textLen, view, filter, filterColor);
    if (hasStep != 0) h->stack.reset(m, baseStep);
    else h->stack.reset(m);
  }

  void stencil_history_pushMemento(int handle, const double* nums, int numsLen,
                                   const std::uint8_t* text, int textLen, const double* view,
                                   const char* filter, const char* filterColor) {
    HistorySlot* h = histories().get(handle);
    if (h != nullptr) h->stack.push(mementoOf(nums, numsLen, text, textLen, view, filter, filterColor));
  }

  void stencil_history_reset(int handle, int hasStep, int baseStep, const double* nums,
                             int numsLen, const std::uint8_t* text, int textLen) {
    stencil_history_resetMemento(handle, hasStep, baseStep, nums, numsLen, text, textLen,
                                 nullptr, nullptr, nullptr);
  }

  void stencil_history_push(int handle, const double* nums, int numsLen,
                            const std::uint8_t* text, int textLen) {
    stencil_history_pushMemento(handle, nums, numsLen, text, textLen, nullptr, nullptr, nullptr);
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

  // The step is read with the readers below before the next call on the handle.
  int stencil_history_undo(int handle, int* outSizes) {
    HistorySlot* h = histories().get(handle);
    return h == nullptr ? 0 : retain(h, h->stack.undo(), outSizes);
  }

  int stencil_history_redo(int handle, int* outSizes) {
    HistorySlot* h = histories().get(handle);
    return h == nullptr ? 0 : retain(h, h->stack.redo(), outSizes);
  }

  void stencil_history_readResult(int handle, double* nums, std::uint8_t* text) {
    const HistorySlot* h = histories().get(handle);
    if (h != nullptr) abi::encodeLines(h->result.lines, nums, text);
  }

  // 1 with out[0..5] = x, y, width, height, quarters, mirrored when the step carries a view, else 0.
  int stencil_history_readView(int handle, double* out) {
    const HistorySlot* h = histories().get(handle);
    if (h == nullptr || !h->result.hasView) return 0;
    const EditorMemento& m = h->result;
    if (out != nullptr) {
      out[0] = m.crop.x; out[1] = m.crop.y; out[2] = m.crop.width; out[3] = m.crop.height;
      out[4] = m.quarters;
      out[5] = m.mirrored ? 1.0 : 0.0;
    }
    return 1;
  }

  // which 0 = the filter mode, 1 = its colour; "" when the step leaves the filter live.
  const char* stencil_history_readFilter(int handle, int which) {
    const HistorySlot* h = histories().get(handle);
    if (h == nullptr) return "";
    return (which == 0 ? h->result.filter : h->result.filterColor).c_str();
  }

}
