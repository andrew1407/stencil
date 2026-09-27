#include "CanvasWidget.hpp"
#include "gestureRoutes.hpp"

#include <QMouseEvent>

namespace stencil::gui {

  void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (image.isNull()) {
      idle.setHover(!idle.hidden && idle.cardRect().contains(event->position()));
      return;
    }
    idle.setHover(false);

    // The divider's drag outranks a hold, and a hold every other gesture. Moving past the tolerance
    // before the hold fires aborts it; while drawing it updates the ghost preview.
    if (!gesture.is(Gesture::COMPARE_SPLIT) && hold.ctl.engaged()) {
      const core::HoldEvent ev =
          hold.ctl.pointerMove(event->pos().x(), event->pos().y(), holdNowMs());
      if (ev.action == core::HoldAction::ABORT) {
        stopHold();
      } else if (ev.action == core::HoldAction::PREVIEW) {
        hold.preview = core::Point{ev.x / scale, ev.y / scale};
        hold.hasPreview = true;
        update();
      }
      emit hoverLeft();     // drop a tooltip left over from before the hold started
      return;
    }
    if (const auto move = GestureRoutes::of(gesture.kind).move) {
      move(*this, event);
      return;
    }

    if (!compareHoldOriginal && event->modifiers() == Qt::NoModifier &&
        isSplitCompare() && nearCompareDivider(event->pos())) {
      setCursor(compareMode == CompareMode::VERTICAL ? Qt::SplitHCursor : Qt::SplitVCursor);
      emit hoverLeft();  // the divider is the affordance; drop any tooltip under it
      return;
    }

    const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());

    if (compareReadOnly()) {
      // Comparing is read-only EDITING: no ring or cursor affordance, but the coordinate readout
      // and hover tooltip keep following the cursor.
      unsetCursor();
      emit hovered(ip.x, ip.y);
      emit hoverDetail(ip.x, ip.y, event->globalPosition().toPoint(),
                       event->modifiers());
      return;
    }

    // Repaint only the lines whose ring or tint moved (browser canvasMouseMove -> point hover ring).
    const int wasHover = hover.lineIdx, wasOver = hover.overLineIdx;
    if (updateHover(ip.x, ip.y)) {
      QRect dirty;
      for (int idx : {wasHover, wasOver, hover.lineIdx, hover.overLineIdx})
        dirty = dirty.united(lineRect(idx));
      update(dirty);
    }

    applyHoverCursor(ip, event->modifiers());

    emit hovered(ip.x, ip.y);
    emit hoverDetail(ip.x, ip.y, event->globalPosition().toPoint(),
                     event->modifiers());
  }

}  // namespace stencil::gui
