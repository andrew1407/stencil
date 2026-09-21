#include "CanvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "CanvasWidget.hpp"

#include <QMouseEvent>

namespace stencil::gui {

  void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (image.isNull()) {
      setIdleCardHover(!idleHintHidden && idleCardRect.contains(event->position()));
      return;
    }
    setIdleCardHover(false);

    if (draggingCompareSplit) {
      const double f = compareMode == "vertical"
                           ? event->pos().x() / (image.width() * scale)
                           : event->pos().y() / (image.height() * scale);
      setCompareSplit(f);   // clamps + repaints
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    // Moving past the tolerance before the hold fires aborts; while drawing it updates the ghost preview.
    if (hold.engaged()) {
      const core::HoldEvent ev =
          hold.pointerMove(event->pos().x(), event->pos().y(), holdNowMs());
      if (ev.action == core::HoldAction::ABORT) {
        stopHold();
      } else if (ev.action == core::HoldAction::PREVIEW) {
        holdPreview = core::Point{ev.x / scale, ev.y / scale};
        holdHasPreview = true;
        update();
      }
      emit hoverLeft();     // drop a tooltip left over from before the hold started
      return;
    }

    // Shift switches segment/line modes live from the original snapshot so toggling never accumulates.
    if (dragKind != DragKind::NONE) {
      const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());
      const bool shift = bool(event->modifiers() & Qt::ShiftModifier);
      dragMoved = true;
      // Repaint only the lines that move, not the whole zoomed page.
      const QRect before = dragRect();
      updateDrag(ip, shift);
      update(before.united(dragRect()));
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    if (panning) {
      // Delta in GLOBAL coords: the scroll we trigger moves this widget under the pointer.
      const QPoint gp = event->globalPosition().toPoint();
      const QPoint d = gp - lastPanPos;
      lastPanPos = gp;
      emit panBy(d.x(), d.y(), bool(event->modifiers() & Qt::ShiftModifier));
      emit hoverLeft();     // drop a tooltip left over from before the pan started
      return;
    }

    if (zoomRectActive) {
      const QRect before = bandRect(zoomRectStart, zoomRectEnd);
      zoomRectEnd = event->pos();
      update(before.united(bandRect(zoomRectStart, zoomRectEnd)));
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    if (rectDrawActive) {
      const QRect before = bandRect(rectDrawStart, rectDrawEnd);
      rectDrawEnd = event->pos();
      update(before.united(bandRect(rectDrawStart, rectDrawEnd)));
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    if (!compareHoldOriginal && event->modifiers() == Qt::NoModifier &&
        (compareMode == "vertical" || compareMode == "horizontal") &&
        nearCompareDivider(event->pos())) {
      setCursor(compareMode == "vertical" ? Qt::SplitHCursor : Qt::SplitVCursor);
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
    const int wasHover = hoverLineIdx, wasOver = hoverOverLineIdx;
    if (updateHover(ip.x, ip.y)) {
      QRect dirty;
      for (int idx : {wasHover, wasOver, hoverLineIdx, hoverOverLineIdx})
        dirty = dirty.united(lineRect(idx));
      update(dirty);
    }

    applyHoverCursor(ip, event->modifiers());

    emit hovered(ip.x, ip.y);
    emit hoverDetail(ip.x, ip.y, event->globalPosition().toPoint(),
                     event->modifiers());
  }

  // `shift` switches segment/line modes live from the original snapshot. Caller sets dragMoved and repaints.
  void CanvasWidget::updateDrag(const core::Point& ip, bool shift) {
    if (dragKind == DragKind::POINT) {
      core::Line* line =
          (dragLineIdx < 0) ? &currentLine : &lines[dragLineIdx];
      if (dragPtIdx1 >= 0 &&
          dragPtIdx1 < static_cast<int>(line->points.size())) {
        line->points[dragPtIdx1] = ip;  // snap point to cursor
      }
    } else {
      const double dx = ip.x - dragStart.x;
      const double dy = ip.y - dragStart.y;
      if (dragKind == DragKind::LINE && !dragMultiOrig.empty()) {
        for (auto& entry : dragMultiOrig) {
          const int li = entry.first;
          if (li < 0 || li >= static_cast<int>(lines.size())) continue;
          auto& pts = lines[li].points;
          const auto& orig = entry.second;
          for (std::size_t i = 0; i < pts.size() && i < orig.size(); ++i) {
            pts[i].x = orig[i].x + dx;
            pts[i].y = orig[i].y + dy;
          }
        }
        return;
      }
      // A LINE drag always moves the whole line even when Shift lifts a beat before the mouse —
      // degrading to the grabbed segment would snap the rest back on commit.
      core::Line& line = lines[dragLineIdx];
      const bool whole = (dragKind == DragKind::LINE) ? true : shift;
      if (whole) {
        for (std::size_t i = 0; i < line.points.size(); ++i) {
          line.points[i].x = dragOrig[i].x + dx;
          line.points[i].y = dragOrig[i].y + dy;
        }
      } else {
        line.points = dragOrig;  // reset, then move only the two endpoints
        for (int pi : {dragPtIdx1, dragPtIdx2}) {
          line.points[pi].x = dragOrig[pi].x + dx;
          line.points[pi].y = dragOrig[pi].y + dy;
        }
      }
    }
  }

}  // namespace stencil::gui
