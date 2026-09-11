#include "canvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "canvasWidget.hpp"

#include <QMouseEvent>

namespace stencil::gui {

  void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (image_.isNull()) {
      // Idle: the only thing tracking the cursor is the "＋ Blank image" card's hover fill.
      setIdleCardHover(!idleHintHidden_ && idleCardRect_.contains(event->position()));
      return;
    }
    setIdleCardHover(false);

    // Compare divider drag takes priority over every other gesture.
    if (draggingCompareSplit_) {
      const double f = compareMode_ == "vertical"
                           ? event->pos().x() / (image_.width() * scale_)
                           : event->pos().y() / (image_.height() * scale_);
      setCompareSplit(f);   // clamps + repaints
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    // Hold-to-draw: while armed/drawing, feed the controller. Moving past the
    // tolerance before the hold fires aborts (a normal click/drag); moving while
    // drawing updates the ghost-line preview. Suppress hover while engaged.
    if (hold_.engaged()) {
      const core::HoldEvent ev =
          hold_.pointerMove(event->pos().x(), event->pos().y(), holdNowMs());
      if (ev.action == core::HoldAction::Abort) {
        stopHold();
      } else if (ev.action == core::HoldAction::Preview) {
        holdPreview_ = core::Point{ev.x / scale_, ev.y / scale_};
        holdHasPreview_ = true;
        update();
      }
      emit hoverLeft();     // drop a tooltip left over from before the hold started
      return;
    }

    // Active Alt-drag gesture (port of drawingApp.js #dragMove ~1701). One of the
    // point/segment/line moves; Shift switches segment/line modes live from the
    // original snapshot so toggling Shift never accumulates.
    if (dragKind_ != DragKind::None) {
      const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());
      const bool shift = bool(event->modifiers() & Qt::ShiftModifier);
      dragMoved_ = true;
      // Only the lines that move: a whole-widget repaint per mouse move redraws the
      // entire zoomed page to follow one stroke.
      const QRect before = dragRect();
      updateDrag(ip, shift);
      update(before.united(dragRect()));
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    if (panning_) {
      // Drag pan: scrollLeft -= dx (Shift = faster; MainWindow applies the speed).
      // Delta in global cursor coords so the scroll we trigger — which moves this
      // widget under the pointer — doesn't feed back into the next sample (jitter).
      const QPoint gp = event->globalPosition().toPoint();
      const QPoint d = gp - lastPanPos_;
      lastPanPos_ = gp;
      emit panBy(d.x(), d.y(), bool(event->modifiers() & Qt::ShiftModifier));
      emit hoverLeft();     // drop a tooltip left over from before the pan started
      return;
    }

    if (zoomRectActive_) {
      const QRect before = bandRect(zoomRectStart_, zoomRectEnd_);
      zoomRectEnd_ = event->pos();
      update(before.united(bandRect(zoomRectStart_, zoomRectEnd_)));
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    // extend the rect-draw rubber band (drawingApp.js mousemove ~815).
    if (rectDrawActive_) {
      const QRect before = bandRect(rectDrawStart_, rectDrawEnd_);
      rectDrawEnd_ = event->pos();
      update(before.united(bandRect(rectDrawStart_, rectDrawEnd_)));
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    // Compare split: show a resize cursor over the movable divider (skip the normal hover
    // cursor + tooltip so the affordance reads clearly).
    if (!compareHoldOriginal_ && event->modifiers() == Qt::NoModifier &&
        (compareMode_ == "vertical" || compareMode_ == "horizontal") &&
        nearCompareDivider(event->pos())) {
      setCursor(compareMode_ == "vertical" ? Qt::SplitHCursor : Qt::SplitVCursor);
      emit hoverLeft();  // the divider is the affordance; drop any tooltip under it
      return;
    }

    const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());

    if (compareReadOnly()) {
      // Comparing is read-only EDITING: no hover ring or cursor affordance, but
      // the coordinate readout and hover tooltips are information and must keep
      // following the cursor (MainWindow drops tooltips the "before" half covers).
      unsetCursor();
      emit hovered(ip.x, ip.y);
      emit hoverDetail(ip.x, ip.y, event->globalPosition().toPoint(),
                       event->modifiers());
      return;
    }

    // Hover ring: track the point under the cursor and repaint when it changes
    // (drawingApp.js canvasMouseMove ~1463 -> renderer point hover ring). Only the lines
    // whose ring or tint moved are repainted, not the whole zoomed page.
    const int wasHover = hoverLineIdx_, wasOver = hoverOverLineIdx_;
    if (updateHover(ip.x, ip.y)) {
      QRect dirty;
      for (int idx : {wasHover, wasOver, hoverLineIdx_, hoverOverLineIdx_})
        dirty = dirty.united(lineRect(idx));
      update(dirty);
    }

    // Cursor affordance (drawingApp.js canvasMouseMove ~1476).
    applyHoverCursor(ip, event->modifiers());

    emit hovered(ip.x, ip.y);
    emit hoverDetail(ip.x, ip.y, event->globalPosition().toPoint(),
                     event->modifiers());
  }

  // Active Alt-drag move (port of drawingApp.js #dragMove ~1701): point/segment/
  // line; `shift` switches segment/line modes live from the original snapshot so
  // toggling Shift never accumulates. Caller sets dragMoved_ and repaints.
  void CanvasWidget::updateDrag(const core::Point& ip, bool shift) {
    if (dragKind_ == DragKind::Point) {
      core::Line* line =
          (dragLineIdx_ < 0) ? &currentLine_ : &lines_[dragLineIdx_];
      if (dragPtIdx1_ >= 0 &&
          dragPtIdx1_ < static_cast<int>(line->points.size())) {
        line->points[dragPtIdx1_] = ip;  // snap point to cursor
      }
    } else {
      const double dx = ip.x - dragStart_.x;
      const double dy = ip.y - dragStart_.y;
      // Multi-select whole-line drag: translate EVERY selected line together.
      if (dragKind_ == DragKind::Line && !dragMultiOrig_.empty()) {
        for (auto& entry : dragMultiOrig_) {
          const int li = entry.first;
          if (li < 0 || li >= static_cast<int>(lines_.size())) continue;
          auto& pts = lines_[li].points;
          const auto& orig = entry.second;
          for (std::size_t i = 0; i < pts.size() && i < orig.size(); ++i) {
            pts[i].x = orig[i].x + dx;
            pts[i].y = orig[i].y + dy;
          }
        }
        return;
      }
      // Segment / Line drags translate from the snapshot by (dx, dy). A LINE drag
      // always moves the whole line even when Shift lifts a beat before the mouse —
      // degrading to the grabbed segment would snap the rest back on commit.
      core::Line& line = lines_[dragLineIdx_];
      const bool whole = (dragKind_ == DragKind::Line) ? true : shift;
      if (whole) {
        for (std::size_t i = 0; i < line.points.size(); ++i) {
          line.points[i].x = dragOrig_[i].x + dx;
          line.points[i].y = dragOrig_[i].y + dy;
        }
      } else {
        line.points = dragOrig_;  // reset, then move only the two endpoints
        for (int pi : {dragPtIdx1_, dragPtIdx2_}) {
          line.points[pi].x = dragOrig_[pi].x + dx;
          line.points[pi].y = dragOrig_[pi].y + dy;
        }
      }
    }
  }

}  // namespace stencil::gui
