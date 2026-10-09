#include "iconDrag.hpp"
#include "../uiTimings.hpp"
#include <cmath>
#include <functional>
#include <vector>

// The drag's pure state machine: slop, start, move, release and abort, driven by tests directly.
namespace stencil::support {

  IconDragMachine::IconDragMachine(IconDragHooks hooks, std::function<QRect()> originRect,
                                   std::function<QWidget*(const QPoint&)> targetAt)
      : hooks(std::move(hooks)), originRect(std::move(originRect)), targetAt(std::move(targetAt)) {}

  IconDragPoint IconDragMachine::at(const QPoint& global) const {
    return {global, originRect && originRect().contains(global), targetAt ? targetAt(global) : nullptr};
  }

  void IconDragMachine::press(const QPoint& global) {
    pressAt = global;
    pressed = true;
    dragging = false;
  }

  bool IconDragMachine::move(const QPoint& global) {
    if (!pressed) return false;
    if (!dragging) {
      const QPoint d = global - pressAt;
      if (std::hypot(d.x(), d.y()) <= uiTimings().pressSlopPx) return false;
      if (hooks.start && !hooks.start(pressAt, global)) {
        pressed = false;
        return false;
      }
      dragging = true;
      ++dragsStarted();
    }
    if (hooks.move) hooks.move(at(global));
    return true;
  }

  bool IconDragMachine::release(const QPoint& global) {
    const bool was = dragging;
    pressed = dragging = false;
    if (!was) return false;
    const IconDragPoint p = at(global);
    if (p.overOrigin) {
      if (hooks.cancel) hooks.cancel();
    } else if (hooks.drop) {
      hooks.drop(p);
    }
    return true;
  }

  bool IconDragMachine::abort() {
    const bool was = dragging;
    pressed = dragging = false;
    if (was && hooks.cancel) hooks.cancel();
    return was;
  }

  // A drag in flight anywhere, and what waits for its drop (afterIconDrag).
  namespace {
    int liveDrags = 0;
    std::vector<std::function<void()>>& waiting() {
      static std::vector<std::function<void()>> fns;
      return fns;
    }
  }  // namespace

  bool anyIconDragActive() { return liveDrags > 0; }

  void afterIconDrag(std::function<void()> fn) {
    if (liveDrags > 0) waiting().push_back(std::move(fn));
    else if (fn) fn();
  }

  void iconDragBegan() { ++liveDrags; }

  void iconDragEnded() {
    if (liveDrags == 0 || --liveDrags > 0) return;
    std::vector<std::function<void()>> run;
    run.swap(waiting());
    for (const auto& fn : run) if (fn) fn();
  }

}  // namespace stencil::support
