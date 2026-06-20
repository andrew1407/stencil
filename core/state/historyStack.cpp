#include "historyStack.hpp"

namespace stencil::core {

  // C++ value semantics give us the deep copy the JS version emulates with
  // JSON.parse(JSON.stringify(...)) — assigning Lines copies the points.

  // Matches the JS constructor: an empty history with the cursor at -1. (A
  // non-empty initial history would make canRedo() wrongly true on a fresh
  // stack, since canRedo tests step < size - 1.)
  HistoryStack::HistoryStack() {
    historyStep_ = -1;
  }

  void HistoryStack::reset(const Lines& lines) {
    reset(lines, lines.empty() ? -1 : 0);
  }

  void HistoryStack::reset(const Lines& lines, int baseStep) {
    history_.clear();
    // A negative base step means "no current snapshot" (a fresh / empty / imageless
    // load): keep the history empty so canRedo() stays false. Pushing a phantom
    // snapshot here made canRedo() true (step -1 < size 1 - 1 = 0), surfacing a
    // stray redo step right after creating a blank image.
    if (baseStep >= 0) history_.push_back(lines);
    historyStep_ = baseStep;
  }

  void HistoryStack::push(const Lines& lines) {
    ++historyStep_;
    history_.resize(static_cast<std::size_t>(historyStep_));  // drop redo branch
    history_.push_back(lines);
  }

  bool HistoryStack::canUndo() const {
    return historyStep_ >= 0;
  }

  bool HistoryStack::canRedo() const {
    return historyStep_ < static_cast<int>(history_.size()) - 1;
  }

  std::optional<Lines> HistoryStack::undo() {
    if (historyStep_ > 0) {
      --historyStep_;
      return history_[static_cast<std::size_t>(historyStep_)];
    }
    if (historyStep_ == 0) {
      historyStep_ = -1;
      return Lines{};
    }
    return std::nullopt;
  }

  std::optional<Lines> HistoryStack::redo() {
    if (historyStep_ < static_cast<int>(history_.size()) - 1) {
      ++historyStep_;
      return history_[static_cast<std::size_t>(historyStep_)];
    }
    return std::nullopt;
  }

}
