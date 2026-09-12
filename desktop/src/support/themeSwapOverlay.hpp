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

    static double bezierY(double t, double x1, double y1, double x2, double y2);
    static qreal swapEase(qreal t) { return bezierY(t, 0.4, 0.25, 0.95, 1.0); }

    // The GRAIN's curve, cubic-bezier(0.22, 0.55, 0.3, 1) — what the browser's
    // .swap-dust-mote keyframes ran on, and what both surfaces now evaluate instead of
    // declaring (browser motion.js swapDustEase). Sampled to the same 256 steps it
    // tabulates, both ends pinned: the solver only bisects to within a hair of 0 and 1,
    // and that hair is enough to leave a spent grain a fraction lit.
    static constexpr int kGrainSteps = 256;
    static double grainEase(double t);
    // Q_OBJECT-free by design, so findChildren<T>() can't reach it — tests (and anything
    // else) locate a live wipe by this name instead.
    static constexpr const char* kObjectName = "stencilThemeSwap";

    // The front (browser motion.js swapEdgePolygon ← dustCloud.js edgeJitter)
    // The wipe's edge wears the particle style: a polygon ring whose vertices ride the same
    // easing, each pushed off the nominal radius by the style's own recipe. Keep the
    // numbers in step with the browser's EDGE table.
    static constexpr int kEdgePoints = 240;
    static constexpr double kTau = 6.28318530717958648;   // M_PI is not portable (MSVC)
    static constexpr double kWaterWaves = 9, kWaterAmp = 0.028, kWaterRipple = 17, kWaterRippleAmp = 0.008;
    static constexpr double kFireTongues = 20, kFireBase = 0.05, kFireVary = 0.05, kFireDip = 0.012, kFireJag = 0.006;
    static double edgeJitter(support::ParticleStyle s, int k, int points = kEdgePoints);
    static double edgeDipOf(support::ParticleStyle s);
    static double edgeBaseOf(support::ParticleStyle s) { return 1 + edgeDipOf(s) + 0.012; }

    static double edgeRadiusAt(int k, double e, double full, support::ParticleStyle s = support::ParticleStyle::Dust);

    // Dust in the wipe's wake (browser motion.js swapDustSpecs — keep in step)
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

    static double dustNoise(int a, int b);

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
                           support::ParticleStyle s = support::ParticleStyle::Dust);

    void seedDust(const QColor& accent, const QColor& shade = QColor(), bool dark = false);

    static ThemeSwapOverlay* capture(QWidget* host, QPoint origin = QPoint(-1, -1));

    void start();

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    ThemeSwapOverlay(QWidget* host, const QPixmap& snap);

    QPixmap snap_;
    QPoint origin_{-1, -1};   // host coords; -1 = fall back to the centre
    double full_ = 0.0;       // the radius that reaches the furthest corner (start())
    double timeMs_ = 0.0;     // the shared clock: wipe over [0, kSwapMs], wake beyond it
    QElapsedTimer clock_;     // …read off this wall clock (start())
    support::MoteSprites sprites_;   // the wake's grains, drawn once each and blitted
    bool dust_ = false;       // armed by seedDust — without it the overlay is the old wipe
    QColor dustAccent_;       // the departing accent…
    QColor dustShade_;        // …and its shade: the wake's palette
    bool dustDark_ = false;   // …from the theme it is erasing, which two tints follow
    // The style the front and its wake wear — read when the wipe is captured, so a
    // 'slide' swap (no grain) still cuts its style's edge… a circle, as dust does.
    support::ParticleStyle style_ = support::particleStyle();
  };

}  // namespace stencil::gui
