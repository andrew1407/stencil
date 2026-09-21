#include "CanvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>

// Release, double-click, wheel and the native gestures.

namespace stencil::gui {

  void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    // Finish a compare-divider drag.
    if (draggingCompareSplit) {
      draggingCompareSplit = false;
      unsetCursor();
      return;
    }

    // Releasing after a stroke commits the line and exits drawing; a quick/aborted hold just tears
    // down (selection already happened on press in handleDrawingClick).
    if (holdTimer.isActive() || hold.engaged()) {
      holdTimer.stop();
      const core::HoldEvent ev = hold.pointerUp(holdNowMs());
      if (ev.action == core::HoldAction::COMMIT) {
        holdCommit();
      } else if (holdHasPreview) {
        holdHasPreview = false;
        update();
      }
      return;
    }

    // Finish an Alt-drag (drawingApp.js mouseup). Commit one undo step only when a committed line
    // actually moved; an in-progress-line point edit just refreshes the panel.
    if (dragKind != DragKind::NONE) {
      const bool moved = dragMoved;
      const bool committed = moved && dragLineIdx >= 0;
      const QRect dirty = dragRect();   // before the kind clears it
      dragKind = DragKind::NONE;
      dragLineIdx = dragPtIdx1 = dragPtIdx2 = -1;
      dragOrig.clear();
      dragMultiOrig.clear();
      unsetCursor();
      update(dirty);
      if (committed) commitHistory();   // emits changed()
      if (moved) emit selectionChanged();
      return;
    }
    if (panning) {
      panning = false;
      unsetCursor();
      return;
    }
    if (zoomRectActive) {
      zoomRectActive = false;
      const QRect band = bandRect(zoomRectStart, zoomRectEnd);
      // Convert the swept rubber band to image space and emit. Only act on a
      // rect bigger than 4x4 image px (drawingApp.js mouseup ~882).
      const core::Point a = toImageSpace(zoomRectStart.x(), zoomRectStart.y());
      const core::Point b = toImageSpace(zoomRectEnd.x(), zoomRectEnd.y());
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
    if (rectDrawActive) {
      rectDrawActive = false;
      const core::Point a = toImageSpace(rectDrawStart.x(), rectDrawStart.y());
      const core::Point b = toImageSpace(rectDrawEnd.x(), rectDrawEnd.y());
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
    // ~964). A double-click also fires a press first, which set panning; clear it.
    const bool altLeft = event->button() == Qt::LeftButton &&
                         (event->modifiers() & Qt::AltModifier);
    if (altLeft || event->button() == Qt::MiddleButton) {
      panning = false;
      unsetCursor();
      emit fitRequested();
      return;
    }
    // Plain left double-click on a line erases it (drawingApp.js canvasDblClick).
    if (event->button() == Qt::LeftButton && !isDrawing && !compareReadOnly()) {
      const QPoint pos = event->position().toPoint();
      const core::Point ip = toImageSpace(pos.x(), pos.y());
      const int idx = core::findLineAt(lines, ip.x, ip.y, hitRadius(8.0));
      if (idx != -1) {
        panning = false;   // the press that opened this double-click armed it
        unsetCursor();
        lines.erase(lines.begin() + idx);
        resetStrokeFx();
        clearHoverCache();   // indices shifted
        selectedLines.clear();
        if (selectedLineIdx == idx) selectedLineIdx = -1;
        else if (selectedLineIdx > idx) --selectedLineIdx;
        selectedPoint = -1;
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

    // Ctrl+Shift+wheel with a selected line rotates it about its centre (or the focused point) by
    // 3 deg/tick (drawingApp.js wheel). With no selection it falls through to a fast (Shift) zoom.
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

  // Trackpad pinch arrives as QEvent::NativeGesture - QWidget has no virtual for it. value() is
  // the incremental scale delta, so scale *= (1 + delta), anchored at the cursor.
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
