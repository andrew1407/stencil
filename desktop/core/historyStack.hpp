#pragma once
#include "models.hpp"
#include <optional>

// Line-snapshot undo/redo stack. Port of browser/js/core/historyStack.js,
// preserving its exact cursor semantics — including the "step 0 -> empty lines,
// step -1" undo behavior and redo-branch truncation on push.
namespace stencil::core {

  class HistoryStack {
   public:
    HistoryStack();

    // Initialize from a base snapshot. When `baseStep` is omitted it follows the
    // JS default: 0 if there are lines, else -1.
    void reset(const Lines& lines);
    void reset(const Lines& lines, int baseStep);

    // Push a new snapshot, truncating any redo branch first.
    void push(const Lines& lines);

    bool canUndo() const;
    bool canRedo() const;

    // Step back one snapshot. Returns the lines to apply, or nullopt if there is
    // nothing to undo. At step 0 it returns an empty snapshot and moves to -1.
    std::optional<Lines> undo();

    // Step forward one snapshot, or nullopt if there is nothing to redo.
    std::optional<Lines> redo();

    int step() const { return historyStep_; }
    std::size_t size() const { return history_.size(); }

   private:
    std::vector<Lines> history_;
    int historyStep_ = -1;
  };

}
