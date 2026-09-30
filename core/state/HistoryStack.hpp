#pragma once
#include "cropGeometry.hpp"  // CropRect
#include "models.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Snapshot undo/redo. Port of browser/js/core/historyStack.js, down to the "step 0 -> the
// floor, step -1" undo and the redo-branch truncation on push.
namespace stencil::core {

  // One undo step of the editor: the lines and the view they sit on. A step without a view
  // leaves the view as it is; an empty `filter` leaves the filter a live setting.
  struct EditorMemento {
    Lines lines;
    bool hasView = false;
    CropRect crop;  // width 0: no crop yet
    int quarters = 0;
    std::string filter;
    std::string filterColor;
  };

  template <typename Snapshot>
  class SnapshotHistory {
   public:
    // Depth cap; canon is LIMITS.historyMax in common/config/constants.json, drift-
    // tested in browser/tests/core/history.test.js. cli/pystencil match; bot's 25 is a budget.
    static constexpr std::size_t MAX_STEPS = 64;

    // Without `baseStep` the JS default applies: 0 if there are lines, else -1.
    void reset(const Snapshot& s);
    void reset(const Snapshot& s, int baseStep);

    // Past MAX_STEPS the oldest snapshots drop off the front and the cursor shifts down. The
    // rvalue form takes the snapshot over rather than copying it.
    void push(const Snapshot& s);
    void push(Snapshot&& s);

    bool canUndo() const;
    bool canRedo() const;

    // nullopt when there is nothing to undo; at step 0 the floor — no lines, on the view of
    // the step the stack starts from — moving to -1.
    std::optional<Snapshot> undo();

    std::optional<Snapshot> redo();

    int step() const { return historyStep; }
    std::size_t size() const { return history.size(); }

   private:
    std::vector<Snapshot> history;
    Snapshot floor{};
    int historyStep = -1;
  };

  using HistoryStack = SnapshotHistory<Lines>;
  using EditorHistory = SnapshotHistory<EditorMemento>;

  extern template class SnapshotHistory<Lines>;
  extern template class SnapshotHistory<EditorMemento>;

}
