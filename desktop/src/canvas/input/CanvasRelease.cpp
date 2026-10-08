#include "CanvasWidget.hpp"
#include "gestureRoutes.hpp"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>

// Release, double-click, wheel and the native gestures.

namespace stencil::gui {

  void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    // The divider's drag outranks a hold, and a hold every other gesture. Releasing after a stroke
    // commits the line and exits drawing; a quick or aborted hold just tears down.
    if (!gesture.is(Gesture::COMPARE_SPLIT) && (hold.timer.isActive() || hold.ctl.engaged())) {
      hold.timer.stop();
      const core::HoldEvent ev = hold.ctl.pointerUp(holdNowMs());
      if (ev.action == core::HoldAction::COMMIT) {
        holdCommit();
      } else if (hold.hasPreview) {
        hold.hasPreview = false;
        update();
      }
      return;
    }
    if (const auto release = GestureRoutes::of(gesture.kind).release) {
      release(*this, event);
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    // Alt+left or middle double-click = fit to window (drawingApp.js resetZoom
    // ~964). A double-click also fires a press first, which set gesture.is(Gesture::PAN); clear it.
    const bool altLeft = event->button() == Qt::LeftButton &&
                         (event->modifiers() & Qt::AltModifier);
    if (altLeft || event->button() == Qt::MiddleButton) {
      gesture.end(Gesture::PAN);
      unsetCursor();
      emit fitRequested();
      return;
    }
    // While drawing, the second press breaks the chain instead of dropping a duplicate point.
    if (event->button() == Qt::LeftButton && isDrawing && drawMode == DrawMode::LINE &&
        !(event->modifiers() & Qt::ShiftModifier) && !compareReadOnly()) {
      const QPoint pos = event->position().toPoint();
      breakChain(toImageSpace(pos.x(), pos.y()), /*repeat=*/true);
      return;
    }
    // Plain left double-click on a line erases it (drawingApp.js canvasDblClick).
    if (event->button() == Qt::LeftButton && !isDrawing && !compareReadOnly()) {
      const QPoint pos = event->position().toPoint();
      const core::Point ip = toImageSpace(pos.x(), pos.y());
      const int idx = model::lineAt(lines, shownMarks(), ip.x, ip.y, lineHitRadius());
      if (idx != -1) {
        gesture.end(Gesture::PAN);   // the press that opened this double-click armed it
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
