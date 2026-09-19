#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"
#include "../support/motionPrefs.hpp"

#include <QCursor>
#include <QKeyEvent>

// The hover cursor, the modifier refresh and the idle card's own hover.

namespace stencil::gui {

  // Alt -> move/grab depending on what's under the cursor; otherwise a pointer
  // over a line. Uses the current hoverPointIdx_ (refreshed by updateHover).
  void CanvasWidget::applyHoverCursor(const core::Point& ip,
                                      Qt::KeyboardModifiers mods) {
    if (mods & Qt::AltModifier) {
      bool overTarget;
      if (mods & Qt::ShiftModifier) {
        overTarget = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0)) != -1;
      } else {
        overTarget = (hoverPointIdx_ >= 0) ||
                     core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))
                         .has_value();
      }
      setCursor(overTarget ? Qt::SizeAllCursor : Qt::OpenHandCursor);
    } else if (!isDrawing_) {
      // Crosshair says "click to place a point" (browser parity: layout.css's unconditional
      // `cursor: crosshair` whenever the canvas is drawable) — a plain arrow gave no such
      // affordance. A line still gets its own pointing-hand hint.
      setCursor(core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0)) != -1
                    ? Qt::PointingHandCursor
                    : Qt::CrossCursor);
    } else {
      setCursor(Qt::CrossCursor);   // actively drawing: the aim, not a plain arrow
    }
  }

  // App-wide filter: on a modifier key press/release with the cursor over the
  // canvas, re-apply hover so tooltip + cursor update without a mouse move
  // (browser parity: the same handlers run on keydown/keyup).
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
    if (image_.isNull() || !underMouse()) return;
    if (panning_ || rectDrawActive_ || zoomRectActive_ ||
        dragKind_ != DragKind::NONE) {
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

  // Hover the idle card the way the browser does (.idle-create-btn transitions colour,
  // lift and shadow over 0.2s) rather than snapping between two states.
  void CanvasWidget::setIdleCardHover(bool on) {
    if (idleCardHover_ == on) return;
    idleCardHover_ = on;
    // The hand belongs to the button, so it appears exactly where the click works.
    if (on) setCursor(Qt::PointingHandCursor);
    else unsetCursor();
    if (!idleCardAnim_) {
      idleCardAnim_ = new QVariantAnimation(this);
      idleCardAnim_->setDuration(200);
      idleCardAnim_->setEasingCurve(QEasingCurve::OutCubic);
      connect(idleCardAnim_, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { idleCardHoverT_ = v.toDouble(); update(); });
    }
    idleCardAnim_->stop();
    // Start from wherever the previous run got to, so a quick in-out doesn't jump.
    idleCardAnim_->setStartValue(idleCardHoverT_);
    idleCardAnim_->setEndValue(on ? 1.0 : 0.0);
    idleCardAnim_->start();

    // …the glyph's own settle (iconMotion.json "image"): the ridge draws itself on and the sun drops
    // in. Restarted from 0 on every enter and LEFT TO FINISH on leave — a settle ends at the rest pose,
    // so there is nothing to ease back (iconMotion.hpp IconMotionRunner::enter / leave).
    if (on && !support::motionReduced()) {
      if (!idleGlyphAnim_) {
        idleGlyphAnim_ = new QVariantAnimation(this);
        idleGlyphAnim_->setDuration(int(IDLE_GLYPH_PLAY_MS));
        idleGlyphAnim_->setStartValue(0.0);
        idleGlyphAnim_->setEndValue(IDLE_GLYPH_PLAY_MS);
        connect(idleGlyphAnim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { idleGlyphMs_ = v.toDouble(); update(); });
        // Back to the canon's own rest markup, so nothing marks a settled glyph as posed.
        connect(idleGlyphAnim_, &QVariantAnimation::finished, this,
                [this] { idleGlyphMs_ = -1.0; update(); });
      }
      idleGlyphAnim_->stop();
      idleGlyphAnim_->start();
    }

    // …and the glass sweep the browser gives every button on hover-enter
    // (layout.css ui-shimmer, 0.75s: a light band crossing from -135% to 135%).
    if (!on) return;
    if (!idleShimmerAnim_) {
      idleShimmerAnim_ = new QVariantAnimation(this);
      idleShimmerAnim_->setDuration(750);
      idleShimmerAnim_->setStartValue(0.0);
      idleShimmerAnim_->setEndValue(1.0);
      idleShimmerAnim_->setEasingCurve(QEasingCurve::InOutQuad);
      connect(idleShimmerAnim_, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { idleShimmerT_ = v.toDouble(); update(); });
      connect(idleShimmerAnim_, &QVariantAnimation::finished, this,
              [this] { idleShimmerT_ = -1.0; update(); });
    }
    idleShimmerAnim_->stop();
    idleShimmerT_ = 0.0;
    idleShimmerAnim_->start();
  }

  void CanvasWidget::leaveEvent(QEvent* event) {
    emit hoverLeft();
    emit canvasLeft();
    if (hoverLineIdx_ != -1 || hoverPointIdx_ != -1 || hoverOverLineIdx_ != -1) {
      hoverLineIdx_ = -1;
      hoverPointIdx_ = -1;
      hoverOverLineIdx_ = -1;
      emit canvasHoverChanged(-1, -1, -1);   // panel row tints clear too
      update();
    }
    setIdleCardHover(false);
    QWidget::leaveEvent(event);
  }

}  // namespace stencil::gui
