#include "canvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "canvasWidget.hpp"
#include "hitTest.hpp"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>

// Release, double-click, wheel and the native gestures.

namespace stencil::gui {

  void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    // Finish a compare-divider drag.
    if (draggingCompareSplit_) {
      draggingCompareSplit_ = false;
      unsetCursor();
      return;
    }

    // Finish a hold-to-draw gesture. Releasing after a stroke commits the line and
    // exits drawing; releasing a quick/aborted hold just tears down (selection
    // already happened on press in handleDrawingClick).
    if (holdTimer_.isActive() || hold_.engaged()) {
      holdTimer_.stop();
      const core::HoldEvent ev = hold_.pointerUp(holdNowMs());
      if (ev.action == core::HoldAction::Commit) {
        holdCommit();
      } else if (holdHasPreview_) {
        holdHasPreview_ = false;
        update();
      }
      return;
    }

    // Finish an Alt-drag gesture (drawingApp.js mouseup ~925-949). Commit one
    // undo step only when a committed line actually moved; an in-progress-line
    // point edit just refreshes the panel.
    if (dragKind_ != DragKind::None) {
      const bool moved = dragMoved_;
      const bool committed = moved && dragLineIdx_ >= 0;
      const QRect dirty = dragRect();   // before the kind clears it
      dragKind_ = DragKind::None;
      dragLineIdx_ = dragPtIdx1_ = dragPtIdx2_ = -1;
      dragOrig_.clear();
      dragMultiOrig_.clear();
      unsetCursor();
      update(dirty);
      if (committed) commitHistory();   // emits changed()
      if (moved) emit selectionChanged();
      return;
    }
    if (panning_) {
      panning_ = false;
      unsetCursor();
      return;
    }
    if (zoomRectActive_) {
      zoomRectActive_ = false;
      const QRect band = bandRect(zoomRectStart_, zoomRectEnd_);
      // Convert the swept rubber band to image space and emit. Only act on a
      // rect bigger than 4x4 image px (drawingApp.js mouseup ~882).
      const core::Point a = toImageSpace(zoomRectStart_.x(), zoomRectStart_.y());
      const core::Point b = toImageSpace(zoomRectEnd_.x(), zoomRectEnd_.y());
      const double x1 = std::min(a.x, b.x);
      const double y1 = std::min(a.y, b.y);
      const double w = std::abs(b.x - a.x);
      const double h = std::abs(b.y - a.y);
      update(band);
      if (w > 4.0 && h > 4.0) emit zoomToRect(QRectF(x1, y1, w, h));
      return;
    }
    // commit the drag-to-create rectangle (drawingApp.js mouseup ~861). Only
    // act when the swept box exceeds 3 image px in both axes.
    if (rectDrawActive_) {
      rectDrawActive_ = false;
      const core::Point a = toImageSpace(rectDrawStart_.x(), rectDrawStart_.y());
      const core::Point b = toImageSpace(rectDrawEnd_.x(), rectDrawEnd_.y());
      if (std::abs(b.x - a.x) > 3.0 && std::abs(b.y - a.y) > 3.0) {
        createRect(a.x, a.y, b.x, b.y);
      }
      update();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    // Alt+left or middle double-click = fit to window (drawingApp.js resetZoom
    // ~964). A double-click also fires a press first, which set panning_; clear it.
    const bool altLeft = event->button() == Qt::LeftButton &&
                         (event->modifiers() & Qt::AltModifier);
    if (altLeft || event->button() == Qt::MiddleButton) {
      panning_ = false;
      unsetCursor();
      emit fitRequested();
      return;
    }
    // Plain left double-click on a line erases it (drawingApp.js canvasDblClick).
    if (event->button() == Qt::LeftButton && !isDrawing_ && !compareReadOnly()) {
      const QPoint pos = event->position().toPoint();
      const core::Point ip = toImageSpace(pos.x(), pos.y());
      const int idx = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0));
      if (idx != -1) {
        panning_ = false;   // the press that opened this double-click armed it
        unsetCursor();
        lines_.erase(lines_.begin() + idx);
        resetStrokeFx();
        clearHoverCache();   // indices shifted
        selectedLines_.clear();
        if (selectedLineIdx_ == idx) selectedLineIdx_ = -1;
        else if (selectedLineIdx_ > idx) --selectedLineIdx_;
        selectedPoint_ = -1;
        commitHistory();
        update();
        emit changed();
        emit selectionChanged();
        return;
      }
    }
    QWidget::mouseDoubleClickEvent(event);
  }

  void CanvasWidget::wheelEvent(QWheelEvent* event) {
    // drawingApp.js wheel (~655). Use whichever axis carries the delta: with a
    // modifier held, X11/GNOME often reports the wheel on angleDelta().x() with .y()==0.
    const auto mods = event->modifiers();
    const QPoint d = event->angleDelta();
    const int delta = d.y() != 0 ? d.y() : d.x();
    if (delta == 0) {
      event->ignore();
      return;
    }

    // Alt+wheel (no Ctrl): adjust thickness of the line under the cursor; wheel-up
    // thickens (drawingApp.js #adjustThicknessAtCursor ~1810). Ctrl+wheel still zooms.
    if ((mods & Qt::AltModifier) && !(mods & Qt::ControlModifier)) {
      const QPoint wp = event->position().toPoint();
      const core::Point ip = toImageSpace(wp.x(), wp.y());
      adjustThicknessAtCursor(ip.x, ip.y, delta > 0 ? 1 : -1);
      event->accept();
      return;
    }

    // Ctrl+Shift+wheel with a selected line: rotate it about its center (or the
    // focused point) by 3 deg/tick (drawingApp.js wheel ~666). With no selection
    // it falls through to a fast (Shift) zoom.
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) &&
        selectionCount() >= 1) {
      rotateSelectedLine((delta > 0 ? 1.0 : -1.0) * (M_PI / 60.0));
      event->accept();
      return;
    }

    // Ctrl+wheel: zoom toward the cursor. Shift triples the step.
    if (mods & Qt::ControlModifier) {
      const bool fast = bool(mods & Qt::ShiftModifier);
      emit zoomAtCursor(delta > 0 ? 1 : -1, event->position().toPoint(), fast);
      event->accept();
      return;
    }

    // Plain wheel: let the scroll area handle it.
    event->ignore();
  }

  // Trackpad pinch (macOS/Wayland) arrives as QEvent::NativeGesture — QWidget has
  // no dedicated virtual for it. value() is the incremental scale delta per event;
  // multiply the current scale by (1 + delta), anchored at the cursor.
  bool CanvasWidget::event(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
      auto* g = static_cast<QNativeGestureEvent*>(e);
      if (g->gestureType() == Qt::ZoomNativeGesture) {
        const double factor = 1.0 + g->value();
        if (factor > 0.0 && factor != 1.0)
          emit zoomByFactorAt(factor, g->position().toPoint());
        g->accept();
        return true;
      }
    }
    return QWidget::event(e);
  }

}  // namespace stencil::gui
