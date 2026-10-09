#include "HistoryStack.hpp"

#include <utility>  // std::move

namespace stencil::core {

  namespace {
    const Lines& linesOf(const Lines& l) { return l; }
    const Lines& linesOf(const EditorMemento& m) { return m.lines; }

    template <typename Snapshot>
    std::size_t pointsOf(const Snapshot& s) {
      std::size_t n = 0;
      for (const Line& l : linesOf(s)) n += l.points.size();
      return n;
    }

    // The step -1 stop keeps a memento's view; a Lines snapshot has none to keep.
    Lines floorOf(const Lines&) { return {}; }
    EditorMemento floorOf(const EditorMemento& m) {
      EditorMemento f = m;
      f.lines.clear();
      return f;
    }
  }  // namespace

  template <typename Snapshot>
  void SnapshotHistory<Snapshot>::reset(const Snapshot& s) {
    reset(s, linesOf(s).empty() ? -1 : 0);
  }

  template <typename Snapshot>
  void SnapshotHistory<Snapshot>::reset(const Snapshot& s, int baseStep) {
    history.clear();
    // A negative base step is "no current snapshot": an empty history keeps canRedo()
    // false (a phantom snapshot would make step -1 < size - 1).
    if (baseStep >= 0) history.push_back(s);
    historyStep = baseStep;
    floor = floorOf(s);
  }

  template <typename Snapshot>
  void SnapshotHistory<Snapshot>::push(const Snapshot& s) {
    push(Snapshot(s));
  }

  template <typename Snapshot>
  void SnapshotHistory<Snapshot>::push(Snapshot&& s) {
    ++historyStep;
    history.resize(static_cast<std::size_t>(historyStep));  // drop redo branch
    history.push_back(std::move(s));
    // Bound the depth, then the points kept: drop the oldest, shift the cursor down as far, so it
    // still names the snapshot just pushed; the floor takes the view of the last one dropped.
    const std::size_t len = history.size();
    std::size_t drop = len > MAX_STEPS ? len - MAX_STEPS : 0;
    std::size_t kept = 0;
    for (std::size_t i = drop; i < len; ++i) kept += pointsOf(history[i]);
    while (drop + 1 < len && kept > MAX_POINTS) kept -= pointsOf(history[drop++]);
    if (drop > 0) {
      floor = floorOf(history[drop - 1]);
      history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(drop));
      historyStep -= static_cast<int>(drop);
    }
  }

  template <typename Snapshot>
  bool SnapshotHistory<Snapshot>::canUndo() const {
    return historyStep >= 0;
  }

  template <typename Snapshot>
  bool SnapshotHistory<Snapshot>::canRedo() const {
    return historyStep < static_cast<int>(history.size()) - 1;
  }

  template <typename Snapshot>
  std::optional<Snapshot> SnapshotHistory<Snapshot>::undo() {
    if (historyStep > 0) {
      --historyStep;
      return history[static_cast<std::size_t>(historyStep)];
    }
    if (historyStep == 0) {
      historyStep = -1;
      return floor;
    }
    return std::nullopt;
  }

  template <typename Snapshot>
  std::optional<Snapshot> SnapshotHistory<Snapshot>::redo() {
    if (historyStep < static_cast<int>(history.size()) - 1) {
      ++historyStep;
      return history[static_cast<std::size_t>(historyStep)];
    }
    return std::nullopt;
  }

  template class SnapshotHistory<Lines>;
  template class SnapshotHistory<EditorMemento>;

}
