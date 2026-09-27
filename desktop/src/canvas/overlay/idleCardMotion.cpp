#include "IdleCard.hpp"
#include "idleCardMotion.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "../../support/motionPrefs.hpp"

#include <QEasingCurve>
#include <QVariantAnimation>

// The idle card's motion: the arrival when a picture leaves, and the hover's blend, glyph settle and
// glass sweep. Each runs on the host, which repaints on every tick.

namespace stencil::gui {

  // The card grows in from the centre the editor just emptied (browser @keyframes idleCardArrive).
  void IdleCard::startArrival() {
    if (!enterAnim) {
      enterAnim = new QVariantAnimation(host);
      enterAnim->setDuration(IDLE_CARD_ARRIVE_MS);
      enterAnim->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(enterAnim, &QVariantAnimation::valueChanged, host,
              [this](const QVariant& v) { enterT = v.toDouble(); host->update(); });
    }
    enterAnim->stop();
    enterAnim->setStartValue(0.0);
    enterAnim->setEndValue(1.0);
    enterT = 0.0;
    enterAnim->start();
  }

  // Hover the idle card the way the browser does (.idle-create-btn transitions colour,
  // lift and shadow over 0.2s) rather than snapping between two states.
  void IdleCard::setHover(bool on) {
    if (hover == on) return;
    hover = on;
    // The hand belongs to the button, so it appears exactly where the click works.
    if (on) host->setCursor(Qt::PointingHandCursor);
    else host->unsetCursor();
    if (!hoverAnim) {
      hoverAnim = new QVariantAnimation(host);
      hoverAnim->setDuration(200);
      hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(hoverAnim, &QVariantAnimation::valueChanged, host,
              [this](const QVariant& v) { hoverT = v.toDouble(); host->update(); });
    }
    hoverAnim->stop();
    // Start from wherever the previous run got to, so a quick in-out doesn't jump.
    hoverAnim->setStartValue(hoverT);
    hoverAnim->setEndValue(on ? 1.0 : 0.0);
    hoverAnim->start();

    // The glyph's own settle (iconMotion.json "image"). Restarted from 0 on every enter and LEFT TO
    // FINISH on leave - a settle ends at the rest pose (iconMotion.hpp IconMotionRunner).
    if (on && !support::motionReduced()) {
      if (!glyphAnim) {
        glyphAnim = new QVariantAnimation(host);
        glyphAnim->setDuration(int(IDLE_GLYPH_PLAY_MS));
        glyphAnim->setStartValue(0.0);
        glyphAnim->setEndValue(IDLE_GLYPH_PLAY_MS);
        QObject::connect(glyphAnim, &QVariantAnimation::valueChanged, host,
                [this](const QVariant& v) { glyphMs = v.toDouble(); host->update(); });
        // Back to the canon's own rest markup, so nothing marks a settled glyph as posed.
        QObject::connect(glyphAnim, &QVariantAnimation::finished, host,
                [this] { glyphMs = -1.0; host->update(); });
      }
      glyphAnim->stop();
      glyphAnim->start();
    }

    // …and the glass sweep the browser gives every button on hover-enter
    // (layout.css ui-shimmer, 0.75s: a light band crossing from -135% to 135%).
    if (!on) return;
    if (!shimmerAnim) {
      shimmerAnim = new QVariantAnimation(host);
      shimmerAnim->setDuration(750);
      shimmerAnim->setStartValue(0.0);
      shimmerAnim->setEndValue(1.0);
      shimmerAnim->setEasingCurve(shimmerEase());   // the browser's `ease`, not a slow start
      // Mirrored to a property (ShimmerOverlay's idiom), so a test samples the band, not the clock.
      QObject::connect(shimmerAnim, &QVariantAnimation::valueChanged, host, [this](const QVariant& v) {
        shimmerT = v.toDouble();
        host->setProperty("idleSweepProgress", shimmerT);
        host->update();
      });
      QObject::connect(shimmerAnim, &QVariantAnimation::finished, host, [this] {
        shimmerT = -1.0;
        host->setProperty("idleSweepProgress", shimmerT);
        host->update();
      });
    }
    shimmerAnim->stop();
    shimmerT = 0.0;
    shimmerAnim->start();
  }

}  // namespace stencil::gui
