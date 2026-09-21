#include "HistoryStack.hpp"

namespace stencil::core {

  HistoryStack::HistoryStack() {
    historyStep = -1;
  }

  void HistoryStack::reset(const Lines& lines) {
    reset(lines, lines.empty() ? -1 : 0);
  }

  void HistoryStack::reset(const Lines& lines, int baseStep) {
    history.clear();
    // A negative base step is "no current snapshot": an empty history keeps canRedo()
    // false (a phantom snapshot would make step -1 < size - 1).
    if (baseStep >= 0) history.push_back(lines);
    historyStep = baseStep;
  }

  void HistoryStack::push(const Lines& lines) {
    ++historyStep;
    history.resize(static_cast<std::size_t>(historyStep));  // drop redo branch
    history.push_back(lines);
    // Bound the depth: drop the oldest, shift the cursor down as far, so it still names
    // the snapshot just pushed. Undoing off the trimmed front still hits the step -1 stop.
    if (history.size() > MAX_STEPS) {
      const std::size_t drop = history.size() - MAX_STEPS;
      history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(drop));
      historyStep -= static_cast<int>(drop);
    }
  }

  bool HistoryStack::canUndo() const {
    return historyStep >= 0;
  }

  bool HistoryStack::canRedo() const {
    return historyStep < static_cast<int>(history.size()) - 1;
  }

  std::optional<Lines> HistoryStack::undo() {
    if (historyStep > 0) {
      --historyStep;
      return history[static_cast<std::size_t>(historyStep)];
    }
    if (historyStep == 0) {
      historyStep = -1;
      return Lines{};
    }
    return std::nullopt;
  }

  std::optional<Lines> HistoryStack::redo() {
    if (historyStep < static_cast<int>(history.size()) - 1) {
      ++historyStep;
      return history[static_cast<std::size_t>(historyStep)];
    }
    return std::nullopt;
  }

}
