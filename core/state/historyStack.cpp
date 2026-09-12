#include "historyStack.hpp"

namespace stencil::core {

  HistoryStack::HistoryStack() {
    historyStep_ = -1;
  }

  void HistoryStack::reset(const Lines& lines) {
    reset(lines, lines.empty() ? -1 : 0);
  }

  void HistoryStack::reset(const Lines& lines, int baseStep) {
    history_.clear();
    // A negative base step is "no current snapshot": an empty history keeps canRedo()
    // false (a phantom snapshot would make step -1 < size - 1).
    if (baseStep >= 0) history_.push_back(lines);
    historyStep_ = baseStep;
  }

  void HistoryStack::push(const Lines& lines) {
    ++historyStep_;
    history_.resize(static_cast<std::size_t>(historyStep_));  // drop redo branch
    history_.push_back(lines);
    // Bound the depth: drop the oldest, shift the cursor down as far, so it still names
    // the snapshot just pushed. Undoing off the trimmed front still hits the step -1 stop.
    if (history_.size() > MAX_STEPS) {
      const std::size_t drop = history_.size() - MAX_STEPS;
      history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
      historyStep_ -= static_cast<int>(drop);
    }
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
