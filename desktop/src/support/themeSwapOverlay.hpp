#pragma once
// Palette-swap wipe: the desktop port of themeSwap() in browser/js/ui/motion.js.
//
// Qt has no CSS transitions, so the browser's View-Transitions trick is done by hand:
// snapshot the window BEFORE the restyle, lay that snapshot over the (already
// re-themed) window, then erase it with a circle growing from the centre — so the new
// palette floods outward exactly like the browser's. Same duration and easing family.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include <QEasingCurve>
#include <QPainter>
#include <QRegion>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointF>
#include <QVariantAnimation>
#include <QWidget>

#include <cmath>

namespace stencil::gui {

  class ThemeSwapOverlay : public QWidget {
   public:
    // One length across all three surfaces (browser motion.js THEME_SWAP_MS, extension
    // accent.js SWAP_MS) — and, since swapEase below, one curve as well.
    static constexpr int kSwapMs = 280;

    // The easing the radius follows, and it is NOT a plain ease-out. The wipe is a CIRCLE,
    // so the area it has recoloured grows as r²: with OutCubic the circle had covered
    // ~95% of the window by 40% of the duration and the remaining 60% of the time went on
    // a sliver in the far corner, which is why the animation read as a snap followed by
    // nothing. Easing the radius IN slightly makes the AREA grow evenly, so the sweep uses
    // its whole duration and reads as durable. Same shape as the browser's
    // cubic-bezier(0.4, 0.25, 0.95, 1) — see the note over ::view-transition-new(root) in
    // browser/css/animations.css, which records the measurements this came from.
    static qreal swapEase(qreal t) {
      // cubic-bezier(x1, y1, x2, y2) with the browser's control points: solve x(u) = t by
      // bisection (the curve is monotonic in x), then read y(u).
      constexpr double x1 = 0.4, y1 = 0.25, x2 = 0.95, y2 = 1.0;
      double lo = 0.0, hi = 1.0, u = t;
      for (int i = 0; i < 24; i++) {
        u = 0.5 * (lo + hi);
        const double x = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
        if (x < t) lo = u; else hi = u;
      }
      return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
    }
    // Q_OBJECT-free by design, so findChildren<T>() can't reach it — tests (and anything
    // else) locate a live wipe by this name instead.
    static constexpr const char* kObjectName = "stencilThemeSwap";

    // Snapshot `host` as it looks RIGHT NOW. Returns nullptr when there is nothing
    // worth animating (no host, not on screen yet, or a degenerate size) — callers
    // treat that as "just restyle", so a swap never depends on this succeeding.
    // `origin` is where the wipe starts, in host coordinates — the control that was
    // used, so the palette visibly comes out of the button you pressed (browser/
    // extension parity). A null/off-window origin falls back to the centre.
    static ThemeSwapOverlay* capture(QWidget* host, QPoint origin = QPoint(-1, -1)) {
      if (!host || !host->isVisible() || host->width() < 2 || host->height() < 2) return nullptr;
      const QPixmap snap = host->grab();
      if (snap.isNull()) return nullptr;
      auto* fx = new ThemeSwapOverlay(host, snap);
      if (host->rect().contains(origin)) fx->origin_ = origin;
      return fx;
    }

    // Play the wipe. Call AFTER the restyle, so the new palette is what shows through.
    void start() {
      setGeometry(parentWidget()->rect());
      raise();
      show();
      const QPointF c = origin_.x() >= 0 ? QPointF(origin_) : QRectF(rect()).center();
      // Reach the furthest corner FROM THAT POINT — off-centre origins need a bigger
      // radius, or the far corner never gets repainted.
      const double full = std::hypot(std::max(c.x(), width() - c.x()),
                                     std::max(c.y(), height() - c.y()));
      auto* anim = new QVariantAnimation(this);
      anim->setDuration(kSwapMs);
      // Even progress in AREA, not in radius (swapEase above).
      QEasingCurve ease(QEasingCurve::Custom);
      ease.setCustomType(&ThemeSwapOverlay::swapEase);
      anim->setEasingCurve(ease);
      anim->setStartValue(0.0);
      anim->setEndValue(full);
      connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        radius_ = v.toDouble();
        // The WHOLE overlay is repainted each frame, not just the annulus the circle swept.
        // Repainting the ring alone is cheaper and looks identical — right up until a widget
        // UNDERNEATH repaints itself: the restyle that precedes the wipe queues repaints for
        // every control it touched, and each one lands on top of an overlay that is never
        // told to cover it again. That is how the colour chips showed their new dark border
        // while the snapshot around them was still light. Redrawing the full frame puts the
        // snapshot back over anything that surfaced, and it is only a clipped pixmap blit —
        // the expensive version this optimisation was added for was the old PATH clip, which
        // rasterised a subtracted circle over the window every frame.
        update();
      });
      connect(anim, &QVariantAnimation::finished, this, [this] { deleteLater(); });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      if (snap_.isNull()) return;
      QPainter p(this);
      // Everything EXCEPT the circle keeps the old snapshot; inside it the freshly
      // themed window shows through.
      //
      // The hole is a QRegion, not a subtracted QPainterPath. Path clipping is
      // rasterised per frame over the whole window, which made the wipe visibly stutter
      // on a large window; region clipping is a cheap span operation. The cost of that
      // is aliased circle edges, so antialiasing is off here — at this size and speed
      // the stepping is invisible, and a smooth 60fps is worth far more than a smooth
      // edge on a half-second transition.
      const QPoint c = origin_.x() >= 0 ? origin_ : rect().center();
      const int r = qRound(radius_);
      const QRegion hole(QRect(c.x() - r, c.y() - r, 2 * r, 2 * r), QRegion::Ellipse);
      p.setClipRegion(QRegion(rect()).subtracted(hole));
      p.drawPixmap(0, 0, snap_);
    }

   private:
    ThemeSwapOverlay(QWidget* host, const QPixmap& snap) : QWidget(host), snap_(snap) {
      setObjectName(kObjectName);   // findable without a Q_OBJECT (this class stays MOC-free)
      // Click-through: the window underneath is already live and fully interactive —
      // the wipe is decoration and must never eat half a second of input.
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
    }

    QPixmap snap_;
    QPoint origin_{-1, -1};   // host coords; -1 = fall back to the centre
    double radius_ = 0.0;
  };

}  // namespace stencil::gui
