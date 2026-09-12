#pragma once
#include "models.hpp"
#include <cstddef>
#include <optional>

// Line-snapshot undo/redo stack. Port of browser/js/core/historyStack.js, down to the
// "step 0 -> empty lines, step -1" undo and the redo-branch truncation on push.
namespace stencil::core {

  class HistoryStack {
   public:
    // Depth cap; canon is LIMITS.historyMax in browser/js/config/constants.json, drift-
    // tested in browser/tests/history.test.js. cli/pystencil match; bot's 25 is a budget.
    static constexpr std::size_t kMaxSteps = 64;

    HistoryStack();

    // Without `baseStep` the JS default applies: 0 if there are lines, else -1.
    void reset(const Lines& lines);
    void reset(const Lines& lines, int baseStep);

    // Past kMaxSteps the oldest snapshots drop off the front and the cursor shifts down.
    void push(const Lines& lines);

    bool canUndo() const;
    bool canRedo() const;

    // nullopt when there is nothing to undo; at step 0 an empty snapshot, moving to -1.
    std::optional<Lines> undo();

    std::optional<Lines> redo();

    int step() const { return historyStep_; }
    std::size_t size() const { return history_.size(); }

   private:
    std::vector<Lines> history_;
    int historyStep_ = -1;
  };

}
