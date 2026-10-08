#include "CanvasWidget.hpp"

#include <QCursor>
#include <QKeyEvent>

// The hover cursor, the modifier refresh and the pointer leaving the canvas.

namespace stencil::gui {

  // Alt -> move/grab depending on what's under the cursor; otherwise a pointer over a line.
  // Reads the hover updateHover just took at `ip` (hover.pointIdx, hover.overLineIdx).
  void CanvasWidget::applyHoverCursor(const core::Point& ip,
                                      Qt::KeyboardModifiers mods) {
    if (mods & Qt::AltModifier) {
      bool overTarget;
      if (mods & Qt::ShiftModifier) {
        overTarget = hover.overLineIdx != -1;
      } else {
        overTarget = (hover.pointIdx >= 0) ||
                     model::segmentAt(lines, shownMarks(), ip.x, ip.y, grabHitRadius()).has_value();
      }
      setCursor(overTarget ? Qt::SizeAllCursor : Qt::OpenHandCursor);
    } else if (!isDrawing) {
      // Crosshair says "click to place a point" (browser parity: layout.css's unconditional
      // `cursor: crosshair` whenever the canvas is drawable). A line still gets its pointing hand.
      setCursor(hover.overLineIdx != -1 ? Qt::PointingHandCursor : Qt::CrossCursor);
    } else {
      setCursor(Qt::CrossCursor);   // actively drawing: the aim, not a plain arrow
    }
  }

  // App-wide filter: on a modifier press/release with the cursor over the canvas, re-apply hover
  // so tooltip + cursor update without a mouse move (browser parity: same keydown/keyup handlers).
  bool CanvasWidget::eventFilter(QObject* watched, QEvent* event) {
    const QEvent::Type t = event->type();
    if (t == QEvent::KeyPress || t == QEvent::KeyRelease) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (!ke->isAutoRepeat()) {
        const int key = ke->key();
        if (key == Qt::Key_Shift
            || key == Qt::Key_Control
            || key == Qt::Key_Alt
            || key == Qt::Key_AltGr
            || key == Qt::Key_Meta) {
          refreshHoverForModifiers();
        }
      }
    }
    return QWidget::eventFilter(watched, event);
  }

  void CanvasWidget::refreshHoverForModifiers() {
    // Only while idly hovering the canvas — never mid-gesture.
    if (image.isNull() || !underMouse()) return;
    if (gesture.is(Gesture::PAN) || gesture.is(Gesture::RECT_DRAW) || gesture.is(Gesture::ZOOM_RECT) ||
        gesture.dragging()) {
      return;
    }
    const QPoint wp = mapFromGlobal(QCursor::pos());
    if (!rect().contains(wp)) return;

    const core::Point ip = toImageSpace(wp.x(), wp.y());
    // queryKeyboardModifiers() reports the live physical state, which (unlike the
    // key event's own modifiers()) already includes the key being pressed.
    const Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    // Same split as the mouse path: a compare view keeps the readout + tooltip but
    // no hover ring or edit cursor.
    if (compareReadOnly()) {
      unsetCursor();
    } else {
      if (updateHover(ip.x, ip.y)) update();
      applyHoverCursor(ip, mods);
    }
    emit hovered(ip.x, ip.y);
    emit hoverDetail(ip.x, ip.y, QCursor::pos(), mods, /*immediate=*/true);
  }

  void CanvasWidget::leaveEvent(QEvent* event) {
    emit hoverLeft();
    emit canvasLeft();
    if (hover.lineIdx != -1 || hover.pointIdx != -1 || hover.overLineIdx != -1) {
      hover.lineIdx = -1;
      hover.pointIdx = -1;
      hover.overLineIdx = -1;
      emit canvasHoverChanged(-1, -1, -1);   // panel row tints clear too
      update();
    }
    idle.setHover(false);
    QWidget::leaveEvent(event);
  }

}  // namespace stencil::gui
