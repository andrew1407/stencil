#pragma once
// Palette-swap wipe: the desktop port of themeSwap() in browser/js/ui/motion.js.
//
// Qt has no CSS transitions, so the browser's View-Transitions trick is done by hand:
// snapshot the window BEFORE the restyle, lay that snapshot over the (already
// re-themed) window, then erase it with a growing hole whose edge is a ragged, dusty
// front (edgeRadiusAt) trailed by grains of the old palette (dustMoteAt) — so the new
// palette crumbles outward exactly like the browser's. Same duration and easing family.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include <QColor>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QPainter>
#include <QPolygon>
#include <QRegion>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointF>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include "dustKit.hpp"   // support::bezierY / MoteSprites / frameIntervalMs

#include <algorithm>
#include <array>
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
    // cubic-bezier(x1, y1, x2, y2) at t — the shared solver (dustKit.hpp), kept here by
    // name for the headless test and the grain's own curve below.
    static double bezierY(double t, double x1, double y1, double x2, double y2) {
      return support::bezierY(t, x1, y1, x2, y2);
    }
    static qreal swapEase(qreal t) { return bezierY(t, 0.4, 0.25, 0.95, 1.0); }

    // The GRAIN's curve, cubic-bezier(0.22, 0.55, 0.3, 1) — what the browser's
    // .swap-dust-mote keyframes ran on, and what both surfaces now evaluate instead of
    // declaring (browser motion.js swapDustEase). Sampled to the same 256 steps it
    // tabulates, both ends pinned: the solver only bisects to within a hair of 0 and 1,
    // and that hair is enough to leave a spent grain a fraction lit.
    static constexpr int kGrainSteps = 256;
    static double grainEase(double t) {
      static const std::array<double, kGrainSteps + 1> curve = [] {
        std::array<double, kGrainSteps + 1> c{};
        for (int i = 0; i <= kGrainSteps; i++)
          c[i] = bezierY(double(i) / kGrainSteps, 0.22, 0.55, 0.3, 1.0);
        c.front() = 0.0;
        c.back() = 1.0;
        return c;
      }();
      return curve[std::clamp(int(std::lround(t * kGrainSteps)), 0, kGrainSteps)];
    }
    // Q_OBJECT-free by design, so findChildren<T>() can't reach it — tests (and anything
    // else) locate a live wipe by this name instead.
    static constexpr const char* kObjectName = "stencilThemeSwap";

    // ── The front (browser motion.js swapEdgePolygon ← dustCloud.js edgeJitter) ──
    // The wipe's edge wears the particle style: a polygon ring whose vertices ride the same
    // easing, each pushed off the nominal radius by the style's own recipe. Keep the
    // numbers in step with the browser's EDGE table.
    static constexpr int kEdgePoints = 240;
    static constexpr double kTau = 6.28318530717958648;   // M_PI is not portable (MSVC)
    static constexpr double kWaterWaves = 9, kWaterAmp = 0.028, kWaterRipple = 17, kWaterRippleAmp = 0.008;
    static constexpr double kFireTongues = 20, kFireBase = 0.05, kFireVary = 0.05, kFireDip = 0.012, kFireJag = 0.006;
    // Vertex k's reach off the nominal radius, as a share of it.
    static double edgeJitter(support::ParticleStyle s, int k, int points = kEdgePoints) {
      const double t = double(k) / points;
      if (s == support::ParticleStyle::Water)
        return kWaterAmp * std::sin(kTau * kWaterWaves * t) + kWaterRippleAmp * std::sin(kTau * kWaterRipple * t + 1);
      if (s == support::ParticleStyle::Fire) {
        const double tongue = std::floor(t * kFireTongues), u = t * kFireTongues - tongue;
        const double h = kFireBase + kFireVary * dustNoise(int(tongue), 5);
        return h * std::pow(std::sin(kTau / 2 * u), 3) - kFireDip + kFireJag * (dustNoise(k, 7) * 2 - 1);
      }
      return 0.0;
    }
    // The deepest dip inward: the ring's base overshoots by it (plus slack) so the
    // finished front still clears the furthest corner, and the wake hugs just inside it.
    static double edgeDipOf(support::ParticleStyle s) {
      if (s == support::ParticleStyle::Water) return kWaterAmp + kWaterRippleAmp;
      if (s == support::ParticleStyle::Fire) return kFireDip + kFireJag;
      return 0.0;
    }
    static double edgeBaseOf(support::ParticleStyle s) { return 1 + edgeDipOf(s) + 0.012; }

    // Where vertex k of the front is at eased progress `e` (swapEase of the wipe time),
    // `full` being the corner-reaching radius. Pure — the headless test pins it to the
    // same coverage contract browser motion.test.js holds swapEdgePolygon to.
    static double edgeRadiusAt(int k, double e, double full, support::ParticleStyle s = support::ParticleStyle::Dust) {
      return e * full * edgeBaseOf(s) * (1 + edgeJitter(s, k));
    }

    // ── Dust in the wipe's wake (browser motion.js swapDustSpecs — keep in step) ──
    // The torn front kicks up specks that ignite along its edge and settle just behind
    // it, painted in the OLD palette (seedDust) — the paint the front grinds away.
    // Always just INSIDE the clip (behind even the deepest tooth, the 1 − amp band):
    // the browser renders its page through the clip, so its motes cannot be seen ahead
    // of the front, and the two surfaces must agree.
    static constexpr int kDustMotes = 4500;
    static constexpr int kDustLifeMs = 340;
    // A mote never ignites at the very ends of the wipe: at t=0 the ring is a point
    // (nothing to ride), and the last ones still get their whole life before teardown.
    static constexpr double kDustMinT = 0.06;
    static constexpr double kDustMaxT = 0.94;
    // Opacity flares over the first 18% of a grain's life, then falls away (browser
    // motion.js SWAP_DUST_FLARE).
    static constexpr double kGrainFlare = 0.18;

    // Deterministic per-mote jitter — the shared scatter hash (browser tileNoise,
    // DisintegrateOverlay::cellNoise), so every surface's dust is cut from one cloth.
    static double dustNoise(int a, int b) {
      const double h = std::sin(a * 127.1 + b * 311.7) * 43758.5453;
      return h - std::floor(h);
    }

    // One mote of the wake at wipe-time `ms`, everything derived from its index alone.
    // Returns false while it has not ignited, once it has burnt out, or when its home is
    // off screen (`bounds`, with the browser's 16px of margin). Pure — the headless test
    // (themeSwapEase.headless.cpp) pins it to the ring the same way the browser's
    // motion.test.js pins swapDustSpecs.
    struct DustMote {
      double x, y, size, alpha;
      bool accent;   // every fourth grain wears the shade (browser parity)
      double life;   // how far through its life (0..1) — the style's progress…
      double w;      // …its own hash (browser swapDustSpecs `w`)…
      double len;    // …the length of its throw, for dustKit.hpp styleFrame…
      double heading;   // …and the way it flies, for its shape
    };
    static bool dustMoteAt(int i, double ms, const QPointF& origin, double full,
                           const QSizeF& bounds, DustMote* out,
                           support::ParticleStyle s = support::ParticleStyle::Dust) {
      const double n = dustNoise(i, 3), m = dustNoise(i + 57, 11), q = dustNoise(i + 13, 29);
      const double u = kDustMinT + m * (kDustMaxT - kDustMinT);
      const double life = (ms - u * kSwapMs) / kDustLifeMs;
      if (life <= 0.0 || life >= 1.0) return false;
      const double angle = n * kTau;
      // Hug the front: just behind even its deepest dip, so the band of grains and the
      // clip read as one crumbling front.
      const double r = swapEase(u) * full * (1 - edgeDipOf(s)) - q * 6;
      if (r <= 0) return false;
      const double hx = origin.x() + std::cos(angle) * r;
      const double hy = origin.y() + std::sin(angle) * r;
      if (hx < -16 || hy < -16 || hx > bounds.width() + 16 || hy > bounds.height() + 16) return false;
      // The browser's swapDustFrame, op for op: opacity flares over the first 18% of the
      // life and falls away across the rest, each leg on the grain's curve; the throw and
      // the shrink ride one pass of it.
      const double e = grainEase(life);
      const double o = life < kGrainFlare
                           ? grainEase(life / kGrainFlare)
                           : 1.0 - grainEase((life - kGrainFlare) / (1.0 - kGrainFlare));
      const double alpha = (0.75 + q * 0.25) * o;
      if (alpha < 1.0 / 255) return false;   // below one 8-bit step — nothing to paint
      // Chase the front outward, slower than it (the ring accelerates away), plus a
      // sideways breath so the wake churns instead of radiating.
      const double d = 8 + q * 14;
      const double dx = std::round(std::cos(angle) * d + (m - 0.5) * 14);
      const double dy = std::round(std::sin(angle) * d + (0.5 - q) * 14);
      out->x = hx + dx * e;
      out->y = hy + dy * e;
      out->alpha = alpha;
      out->size = (2.5 + n * 3.5) * (1.0 - 0.7 * e);
      out->accent = i % 4 == 0;
      out->life = life;
      out->w = dustNoise(i + 71, 13);
      out->len = std::hypot(dx, dy);
      out->heading = support::headingOf(dx, dy, false);
      return true;
    }

    // Arm the wake, in the palette the wipe is ERASING — call with the colours as they
    // stood BEFORE the restyle: the departing `accent` and its `shade`, the same two
    // colours every cloud wears (browser motion.js swapDustPaint palette).
    void seedDust(const QColor& accent, const QColor& shade = QColor()) {
      if (!accent.isValid()) return;
      dustAccent_ = accent;
      dustShade_ = shade.isValid() ? shade : accent;
      dust_ = true;
    }

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
      full_ = std::hypot(std::max(c.x(), width() - c.x()),
                         std::max(c.y(), height() - c.y()));
      // The overlay outlives the snapshot by the wake: the last motes ignite near the
      // wipe's end and still get their whole life. Without dust nothing changes.
      const int total = kSwapMs + (dust_ ? kDustLifeMs : 0);
      // A plain CLOCK (milliseconds) on a timer at the screen's own refresh rate
      // (dustKit.hpp frameIntervalMs — not Qt's 60Hz animation timer); the wipe reads
      // its radius off swapEase so the AREA grows evenly, and the dust reads its own
      // life off the same clock — one timeline, two consumers.
      clock_.start();
      auto* tick = new QTimer(this);
      tick->setTimerType(Qt::PreciseTimer);
      tick->setInterval(support::frameIntervalMs(this));
      connect(tick, &QTimer::timeout, this, [this, tick, total] {
        timeMs_ = std::min(double(total), clock_.nsecsElapsed() / 1e6);
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
        if (timeMs_ >= total) {
          tick->stop();
          deleteLater();
        }
      });
      tick->start();
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
      // Once the wipe itself is over only the wake is left — no snapshot to lay back.
      if (timeMs_ < kSwapMs) {
        // The hole is the RAGGED front (edgeRadiusAt), not a circle: a torn polygon
        // still clips as cheap spans — the expensive thing this replaced long ago was
        // an antialiased QPainterPath, and a QRegion from a polygon is not that.
        const double e = swapEase(std::min(1.0, timeMs_ / kSwapMs));
        QPolygon front;
        front.reserve(kEdgePoints);
        for (int k = 0; k < kEdgePoints; k++) {
          const double a = k * kTau / kEdgePoints;
          const double r = edgeRadiusAt(k, e, full_, style_);
          front << QPoint(qRound(c.x() + std::cos(a) * r), qRound(c.y() + std::sin(a) * r));
        }
        p.setClipRegion(QRegion(rect()).subtracted(QRegion(front)));
        p.drawPixmap(0, 0, snap_);
      }
      if (!dust_) return;
      // The wake is INSIDE the circle, over the freshly themed window — clip off. Round
      // grains, like every other cloud's — a couple of thousand alive at the peak, each
      // a ~5px disc blitted from the sprite cache (dustKit.hpp MoteSprites: a dozen
      // colours, a handful of sizes), so the wake costs a fraction of a frame.
      p.setClipping(false);
      p.setRenderHint(QPainter::Antialiasing, false);
      p.setRenderHint(QPainter::SmoothPixmapTransform, false);
      DustMote mote;
      for (int i = 0; i < kDustMotes; i++) {
        if (!dustMoteAt(i, timeMs_, QPointF(c), full_, QSizeF(size()), &mote, style_)) continue;
        // Every grain is painted from the departing palette by its mix and its hash
        // (browser spawnSwapDust), in the style's own shape along its heading.
        const support::StyleFrame sf = support::styleFrame(style_, mote.life, mote.life, mote.w, mote.len, timeMs_);
        const double mix = style_ == support::ParticleStyle::Dust ? support::dustMix(mote.w, mote.accent) : sf.mix;
        QColor col = support::tintedStop(dustAccent_, dustShade_, mix, support::tintOf(mote.w));
        col.setAlphaF(std::clamp(mote.alpha * sf.glow, 0.0, 1.0));
        sprites_.draw(p, QPointF(mote.x + sf.sx, mote.y + sf.sy), mote.size / 2 * sf.scale, col,
                      support::grainShape(style_, mote.w), mote.heading);
      }
      p.setOpacity(1.0);
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
    double full_ = 0.0;       // the radius that reaches the furthest corner (start())
    double timeMs_ = 0.0;     // the shared clock: wipe over [0, kSwapMs], wake beyond it
    QElapsedTimer clock_;     // …read off this wall clock (start())
    support::MoteSprites sprites_;   // the wake's grains, drawn once each and blitted
    bool dust_ = false;       // armed by seedDust — without it the overlay is the old wipe
    QColor dustAccent_;       // the departing accent…
    QColor dustShade_;        // …and its shade: the wake's palette
    // The style the front and its wake wear — read when the wipe is captured, so a
    // 'slide' swap (no grain) still cuts its style's edge… a circle, as dust does.
    support::ParticleStyle style_ = support::particleStyle();
  };

}  // namespace stencil::gui
