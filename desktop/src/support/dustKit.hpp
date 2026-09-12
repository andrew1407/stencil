#pragma once
// Shared by every desktop cloud: easing LUTs, the grain sprite cache, and the clock —
// the screen's own refresh rate, not Qt's 60Hz animation timer. Q_OBJECT-free.
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPaintDevice>
#include <QPointF>
#include <QScreen>
#include <QWidget>

#include "motionPrefs.hpp"   // ParticleStyle

#include <algorithm>
#include <array>
#include <cmath>

namespace stencil::support {

  // Particle styles (browser dustCloud.js styleFrame — keep the numbers in step).
  // `mix`: 0 the main colour … 1 its shade. Dust is the identity.
  struct StyleFrame {
    double sx = 0, sy = 0, scale = 1, glow = 1, mix = 0;
  };
  namespace style {
    constexpr double kWaterSagShare = 0.45, kWaterSagMaxPx = 30;      // a drop sags below its line…
    constexpr double kWaterSwayShare = 0.12, kWaterSwayMaxPx = 5;     // …and sways slowly across it
    constexpr double kWaterSwayWaves[2] = {0.8, 1.4};                 // sways per flight, by hash
    constexpr double kWaterSwell = 0.3;                               // grows this much mid-flight
    constexpr double kWaterShimmerDepth = 0.25, kWaterShimmerHz[2] = {1.2, 2.2};
    constexpr double kWaterGlistenHz[2] = {0.6, 1.1};                 // a slow drift between the two colours
    constexpr double kFireLiftShare = 0.6, kFireLiftMaxPx = 44;       // an ember lifts above its line…
    constexpr double kFireWaverShare = 0.08, kFireWaverMaxPx = 4;     // …and wavers quickly across it
    constexpr double kFireWaverWaves[2] = {3, 5};
    constexpr double kFireFlare = 0.35;                               // grows this much mid-flight, when bright
    constexpr double kFireFlickerDepth = 0.55, kFireFlickerHz[2] = {9, 14};
    constexpr double kFireCoolHash = 0.25;   // colour: this share by hash, the rest by distance from home
    constexpr double kPi = 3.14159265358979323846;
    inline double wave(const double range[2], double w) { return range[0] + (range[1] - range[0]) * w; }
  }  // namespace style
  StyleFrame styleFrame(ParticleStyle s, double p, double away, double w, double len, double ms);
  // browser dustCloud.js dustMix: plain grains to halfway by hash, glints wear the shade.
  constexpr double kDustMixSpread = 0.5;
  inline double dustMix(double w, bool glint) { return glint ? 1.0 : w * kDustMixSpread; }
  inline double fract(double v) { return v - std::floor(v); }
  // browser paletteCss / paletteIndex — CSS color-mix in srgb is this same straight mix.
  constexpr int kPaletteStops = 6;
  int paletteIndex(double mix, int stops = kPaletteStops);
  QColor paletteStop(const QColor& accent, const QColor& shade, double mix, int stops = kPaletteStops);
  // Browser twin: dustCloud.js TINT_CSS / tintOf; the share and the mixes are the contract.
  constexpr double kTintShare = 0.34;
  constexpr int kTintStops = 5;
  constexpr double kTintAccentShare = 0.55;   // …of the accent in the pale and the deep one
  constexpr double kTintPaleShare = 0.30;     // …and in the pale one the dark theme swaps in
  int tintOf(double w);
  QColor towards(const QColor& c, int to, double k);
  QColor tintColour(const QColor& accent, int tint, bool dark);
  QColor tintedStop(const QColor& accent, const QColor& shade, double mix, int tint, bool dark);

  double bezierY(double t, double x1, double y1, double x2, double y2);

  // Both ends pinned exactly: the solver's hair off 1 leaves a spent grain a fraction lit.
  // Thousands of grains read it per frame; solving per read was the frame.
  class EaseLut {
   public:
    static constexpr int kSteps = 256;
    EaseLut(double x1, double y1, double x2, double y2);
    double at(double t) const {
      return curve_[std::clamp(int(std::lround(t * kSteps)), 0, kSteps)];
    }
   private:
    std::array<double, kSteps + 1> curve_{};
  };

  // Grain shapes (browser dustCloud.js grainShape / shapePolygon / addGrainPath), each
  // lying along its heading. Geometry in radii — keep the browser's numbers.
  enum class GrainShape { Disc, Oval, Wave, Triangle, Streak };
  // browser dustCloud.js STYLED_CELL_SCALE: a styled grain blits more pixels.
  constexpr double kStyledCellScale = 1.4;
  namespace shape {
    constexpr double kWaterWaveShare = 0.3, kFireStreakShare = 0.4;
    constexpr double kOvalRx = 1.45, kOvalRy = 0.7;
    constexpr double kWaveLen = 3.6, kWaveAmp = 0.42, kWaveWaves = 1.5, kWaveHalf = 0.28;
    constexpr int kWaveSamples = 9;
    constexpr double kTriTip = 1.7, kTriBase = 0.85, kTriHalf = 1.0;
    constexpr double kStreakHead = 1.0, kStreakHeadHalf = 0.42, kStreakTail = 2.6, kStreakTailHalf = 0.1;
  }  // namespace shape
  GrainShape grainShape(ParticleStyle s, double w);
  double headingOf(double dx, double dy, bool fromFar);
  QPolygonF shapePolygon(GrainShape s, const QPointF& at, double r, double a);

  // drawEllipse rasterises every grain from scratch (~9ms for 4000 at 2x); a blit is
  // ~18x cheaper (measured), so a grain is drawn once per (colour, radius, half-pixel
  // phase, heading) and copied. Positions land on half device pixels.
  class MoteSprites {
   public:
    static constexpr double kRadiusStep = 0.25;   // logical px between cached radii
    static constexpr int kMaxRadiusSteps = 63;    // 15.75px — far past any grain
    // The colour axis is unbounded (a theme swap walks a gradient). Over the cap the whole
    // set is dropped and refilled: an LRU eviction scan has no place in the paint loop.
    static constexpr int kMaxCached = 4096;
    static constexpr int kHeadingSteps = 24;   // 15° apart

    void draw(QPainter& p, const QPointF& at, double radius, const QColor& colour,
              GrainShape shape = GrainShape::Disc, double a = 0.0);

    int cached() const { return int(cache_.size()); }

   private:
    static quint32 colourKey(const QColor& c);

    static void drawExact(QPainter& q, GrainShape shape, const QPointF& at, double r, double a);
    static double reachOf(GrainShape s);

    QImage build(double radius, int px, int py, const QColor& colour, GrainShape shape, double a) const;

    QHash<quint32, QImage> cache_;
    double dpr_ = 0;
  };

  int frameIntervalMs(const QWidget* w);

}  // namespace stencil::support
