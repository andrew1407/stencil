#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "../../support/motionPrefs.hpp"

#include <QCursor>
#include <QKeyEvent>

// The hover cursor, the modifier refresh and the idle card's own hover.

namespace stencil::gui {

  // Alt -> move/grab depending on what's under the cursor; otherwise a pointer
  // over a line. Uses the current hoverPointIdx (refreshed by updateHover).
  void CanvasWidget::applyHoverCursor(const core::Point& ip,
                                      Qt::KeyboardModifiers mods) {
    if (mods & Qt::AltModifier) {
      bool overTarget;
      if (mods & Qt::ShiftModifier) {
        overTarget = core::findLineAt(lines, ip.x, ip.y, hitRadius(8.0)) != -1;
      } else {
        overTarget = (hoverPointIdx >= 0) ||
                     core::findNearestSegment(lines, ip.x, ip.y, hitRadius(12.0))
                         .has_value();
      }
      setCursor(overTarget ? Qt::SizeAllCursor : Qt::OpenHandCursor);
    } else if (!isDrawing) {
      // Crosshair says "click to place a point" (browser parity: layout.css's unconditional
      // `cursor: crosshair` whenever the canvas is drawable). A line still gets its pointing hand.
      setCursor(core::findLineAt(lines, ip.x, ip.y, hitRadius(8.0)) != -1
                    ? Qt::PointingHandCursor
                    : Qt::CrossCursor);
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
    if (panning || rectDrawActive || zoomRectActive ||
        dragKind != DragKind::NONE) {
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
    if (idleCardHover == on) return;
    idleCardHover = on;
    // The hand belongs to the button, so it appears exactly where the click works.
    if (on) setCursor(Qt::PointingHandCursor);
    else unsetCursor();
    if (!idleCardAnim) {
      idleCardAnim = new QVariantAnimation(this);
      idleCardAnim->setDuration(200);
      idleCardAnim->setEasingCurve(QEasingCurve::OutCubic);
      connect(idleCardAnim, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { idleCardHoverT = v.toDouble(); update(); });
    }
    idleCardAnim->stop();
    // Start from wherever the previous run got to, so a quick in-out doesn't jump.
    idleCardAnim->setStartValue(idleCardHoverT);
    idleCardAnim->setEndValue(on ? 1.0 : 0.0);
    idleCardAnim->start();

    // The glyph's own settle (iconMotion.json "image"). Restarted from 0 on every enter and LEFT TO
    // FINISH on leave - a settle ends at the rest pose (iconMotion.hpp IconMotionRunner).
    if (on && !support::motionReduced()) {
      if (!idleGlyphAnim) {
        idleGlyphAnim = new QVariantAnimation(this);
        idleGlyphAnim->setDuration(int(IDLE_GLYPH_PLAY_MS));
        idleGlyphAnim->setStartValue(0.0);
        idleGlyphAnim->setEndValue(IDLE_GLYPH_PLAY_MS);
        connect(idleGlyphAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { idleGlyphMs = v.toDouble(); update(); });
        // Back to the canon's own rest markup, so nothing marks a settled glyph as posed.
        connect(idleGlyphAnim, &QVariantAnimation::finished, this,
                [this] { idleGlyphMs = -1.0; update(); });
      }
      idleGlyphAnim->stop();
      idleGlyphAnim->start();
    }

    // …and the glass sweep the browser gives every button on hover-enter
    // (layout.css ui-shimmer, 0.75s: a light band crossing from -135% to 135%).
    if (!on) return;
    if (!idleShimmerAnim) {
      idleShimmerAnim = new QVariantAnimation(this);
      idleShimmerAnim->setDuration(750);
      idleShimmerAnim->setStartValue(0.0);
      idleShimmerAnim->setEndValue(1.0);
      idleShimmerAnim->setEasingCurve(shimmerEase());   // the browser's `ease`, not a slow start
      // Mirrored to a property (ShimmerOverlay's idiom), so a test samples the band, not the clock.
      connect(idleShimmerAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        idleShimmerT = v.toDouble();
        setProperty("idleSweepProgress", idleShimmerT);
        update();
      });
      connect(idleShimmerAnim, &QVariantAnimation::finished, this, [this] {
        idleShimmerT = -1.0;
        setProperty("idleSweepProgress", idleShimmerT);
        update();
      });
    }
    idleShimmerAnim->stop();
    idleShimmerT = 0.0;
    idleShimmerAnim->start();
  }

  void CanvasWidget::leaveEvent(QEvent* event) {
    emit hoverLeft();
    emit canvasLeft();
    if (hoverLineIdx != -1 || hoverPointIdx != -1 || hoverOverLineIdx != -1) {
      hoverLineIdx = -1;
      hoverPointIdx = -1;
      hoverOverLineIdx = -1;
      emit canvasHoverChanged(-1, -1, -1);   // panel row tints clear too
      update();
    }
    setIdleCardHover(false);
    QWidget::leaveEvent(event);
  }

}  // namespace stencil::gui
