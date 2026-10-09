#pragma once
// Dragging a toolbar control: past the press slop a ghost of the control follows the pointer and
// the owner's hooks see the drag; released back over the control it left, the drag is a cancel.
// IconDragMachine is pure (tests drive it); installIconDrag is its widget wiring.
// Browser twin: browser/js/ui/drag/iconDrag.js.
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <functional>

class QAbstractButton;
class QWidget;

namespace stencil::support {

  struct IconDragPoint {
    QPoint global;
    bool overOrigin = false;     // released here, the drag cancels
    QWidget* target = nullptr;   // the widget under the pointer; the ghost takes no mouse
  };

  struct IconDragHooks {
    std::function<bool(const QPoint& from, const QPoint& global)> start;   // false refuses the drag
    std::function<void(const IconDragPoint&)> move;
    std::function<void(const IconDragPoint&)> drop;   // released away from the origin
    std::function<void()> cancel;                     // over the origin, Escape, focus lost
    bool ghost = true;                                // false: the owner draws its own
    bool ghostCentred = false;   // the ghost rides centred on the pointer, not held where it was grabbed
    std::function<QPixmap()> face;   // the ghost's picture; none = the control's own grab
    // A source that is no button (an item view's viewport): the global rect a press there takes
    // hold of, the drag's origin, or an empty one where a press drags nothing.
    std::function<QRect(const QPoint& global)> grab;
  };

  class IconDragMachine {
   public:
    IconDragMachine(IconDragHooks hooks, std::function<QRect()> originRect,
                    std::function<QWidget*(const QPoint&)> targetAt = {});

    bool active() const { return dragging; }
    void press(const QPoint& global);
    bool move(const QPoint& global);      // true while dragging
    bool release(const QPoint& global);   // true when this release ended a drag
    bool abort();

    IconDragHooks hooks;

   private:
    IconDragPoint at(const QPoint& global) const;

    std::function<QRect()> originRect;
    std::function<QWidget*(const QPoint&)> targetAt;
    QPoint pressAt;
    bool pressed = false;
    bool dragging = false;
  };

  // Drags started this session: a deferred open notes it and stands down if a drag began since.
  inline unsigned& dragsStarted() {
    static unsigned started = 0;
    return started;
  }

  // Whether any control is being dragged now; afterIconDrag runs `fn` at once without one, or after
  // the live drag's drop. iconDragBegan/Ended bracket each drag (installIconDrag calls them).
  bool anyIconDragActive();
  void afterIconDrag(std::function<void()> fn);
  void iconDragBegan();
  void iconDragEnded();

  // The drop and the cancel run on the next event-loop turn, after the source has seen its release,
  // so a hook may open a modal; a source that is no button sees no live drag's moves or release.
  void installIconDrag(QWidget* source, IconDragHooks hooks);
  bool iconDragActive(const QWidget* source);

  // A drop target's glow while a drag is live, painted over the window: `on` marks it as
  // available, `over` as the one under the pointer.
  void markDropTarget(QWidget* target, bool on, bool over = false);
  inline constexpr char DROP_GLOW_NAME[] = "dropTargetGlow";

}  // namespace stencil::support
