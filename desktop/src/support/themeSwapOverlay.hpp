#pragma once
// Palette-swap wipe: the desktop port of themeSwap() in browser/js/ui/motion.js — a
// snapshot of the old palette erased by a growing, ragged, dusty hole. Q_OBJECT-free.
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
    // One length across all three surfaces (browser motion.js THEME_SWAP_MS, extension accent.js SWAP_MS).
    static constexpr int kSwapMs = 280;

    static double bezierY(double t, double x1, double y1, double x2, double y2);
    static qreal swapEase(qreal t) { return bezierY(t, 0.4, 0.25, 0.95, 1.0); }

    // cubic-bezier(0.22, 0.55, 0.3, 1), the browser's swapDustEase, sampled to the same 256
    // steps, both ends pinned: the solver leaves a hair at 0 and 1 that keeps a spent grain lit.
    static constexpr int kGrainSteps = 256;
    static double grainEase(double t);
    // Q_OBJECT-free, so findChildren<T>() can't reach it — tests locate a live wipe by this name.
    static constexpr const char* kObjectName = "stencilThemeSwap";

    // The front (browser motion.js swapEdgePolygon ← dustCloud.js edgeJitter). Keep the
    // numbers in step with the browser's EDGE table.
    static constexpr int kEdgePoints = 240;
    static constexpr double kTau = 6.28318530717958648;   // M_PI is not portable (MSVC)
    static constexpr double kWaterWaves = 9, kWaterAmp = 0.028, kWaterRipple = 17, kWaterRippleAmp = 0.008;
    static constexpr double kFireTongues = 20, kFireBase = 0.05, kFireVary = 0.05, kFireDip = 0.012, kFireJag = 0.006;
    static double edgeJitter(support::ParticleStyle s, int k, int points = kEdgePoints);
    static double edgeDipOf(support::ParticleStyle s);
    static double edgeBaseOf(support::ParticleStyle s) { return 1 + edgeDipOf(s) + 0.012; }

    static double edgeRadiusAt(int k, double e, double full, support::ParticleStyle s = support::ParticleStyle::Dust);

    // Dust in the wake (browser motion.js swapDustSpecs — keep in step). Always just INSIDE
    // the clip: the browser renders through the clip, so motes are never seen ahead of the front.
    static constexpr int kDustMotes = 4500;
    static constexpr int kDustLifeMs = 340;
    // Never at the very ends: at t=0 the ring is a point, and the last ones need their whole life.
    static constexpr double kDustMinT = 0.06;
    static constexpr double kDustMaxT = 0.94;
    // Flares over the first 18% of a grain's life (browser motion.js SWAP_DUST_FLARE).
    static constexpr double kGrainFlare = 0.18;

    static double dustNoise(int a, int b);

    // One mote at wipe-time `ms`, derived from its index alone; false while unlit, burnt
    // out, or off screen (16px margin). Pure — themeSwapEase.headless.cpp pins it.
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
    // Read when the wipe is captured, so a 'slide' swap still cuts its style's edge.
    support::ParticleStyle style_ = support::particleStyle();
  };

}  // namespace stencil::gui
