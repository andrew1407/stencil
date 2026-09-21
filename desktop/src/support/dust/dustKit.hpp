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

  // Particle styles (browser dust/cloud.js styleFrame — keep the numbers in step).
  // `mix`: 0 the main colour … 1 its shade. Dust is the identity.
  struct StyleFrame {
    double sx = 0, sy = 0, scale = 1, glow = 1, mix = 0;
  };
  namespace style {
    constexpr double WATER_SAG_SHARE = 0.45, WATER_SAG_MAX_PX = 30;      // a drop sags below its line…
    constexpr double WATER_SWAY_SHARE = 0.12, WATER_SWAY_MAX_PX = 5;     // …and sways slowly across it
    constexpr double WATER_SWAY_WAVES[2] = {0.8, 1.4};                 // sways per flight, by hash
    constexpr double WATER_SWELL = 0.3;                               // grows this much mid-flight
    constexpr double WATER_SHIMMER_DEPTH = 0.25, WATER_SHIMMER_HZ[2] = {1.2, 2.2};
    constexpr double WATER_GLISTEN_HZ[2] = {0.6, 1.1};                 // a slow drift between the two colours
    constexpr double FIRE_LIFT_SHARE = 0.6, FIRE_LIFT_MAX_PX = 44;       // an ember lifts above its line…
    constexpr double FIRE_WAVER_SHARE = 0.08, FIRE_WAVER_MAX_PX = 4;     // …and wavers quickly across it
    constexpr double FIRE_WAVER_WAVES[2] = {3, 5};
    constexpr double FIRE_FLARE = 0.35;                               // grows this much mid-flight, when bright
    constexpr double FIRE_FLICKER_DEPTH = 0.55, FIRE_FLICKER_HZ[2] = {9, 14};
    constexpr double FIRE_COOL_HASH = 0.25;   // colour: this share by hash, the rest by distance from home
    constexpr double PI = 3.14159265358979323846;
    inline double wave(const double range[2], double w) { return range[0] + (range[1] - range[0]) * w; }
  }  // namespace style
  StyleFrame styleFrame(ParticleStyle s, double p, double away, double w, double len, double ms);
  // browser dust/cloud.js dustMix: plain grains to halfway by hash, glints wear the shade.
  constexpr double DUST_MIX_SPREAD = 0.5;
  inline double dustMix(double w, bool glint) { return glint ? 1.0 : w * DUST_MIX_SPREAD; }
  inline double fract(double v) { return v - std::floor(v); }
  // browser paletteCss / paletteIndex — CSS color-mix in srgb is this same straight mix.
  constexpr int PALETTE_STOPS = 6;
  int paletteIndex(double mix, int stops = PALETTE_STOPS);
  QColor paletteStop(const QColor& accent, const QColor& shade, double mix, int stops = PALETTE_STOPS);
  // Browser twin: dust/cloud.js TINT_CSS / tintOf; the share and the mixes are the contract.
  constexpr double TINT_SHARE = 0.34;
  constexpr int TINT_STOPS = 5;
  constexpr double TINT_ACCENT_SHARE = 0.55;   // …of the accent in the pale and the deep one
  constexpr double TINT_PALE_SHARE = 0.30;     // …and in the pale one the dark theme swaps in
  int tintOf(double w);
  QColor towards(const QColor& c, int to, double k);
  QColor tintColour(const QColor& accent, int tint, bool dark);
  QColor tintedStop(const QColor& accent, const QColor& shade, double mix, int tint, bool dark);

  double bezierY(double t, double x1, double y1, double x2, double y2);

  // Both ends pinned exactly: the solver's hair off 1 leaves a spent grain a fraction lit.
  // Thousands of grains read it per frame; solving per read was the frame.
  class EaseLut {
   public:
    static constexpr int STEPS = 256;
    EaseLut(double x1, double y1, double x2, double y2);
    double at(double t) const {
      return curve[std::clamp(int(std::lround(t * STEPS)), 0, STEPS)];
    }
   private:
    std::array<double, STEPS + 1> curve{};
  };

  // Grain shapes (browser dust/cloud.js grainShape / shapePolygon / addGrainPath), each
  // lying along its heading. Geometry in radii — keep the browser's numbers.
  enum class GrainShape { DISC, OVAL, WAVE, TRIANGLE, STREAK };
  // browser dust/cloud.js STYLED_CELL_SCALE: a styled grain blits more pixels.
  constexpr double STYLED_CELL_SCALE = 1.4;
  namespace shape {
    constexpr double WATER_WAVE_SHARE = 0.3, FIRE_STREAK_SHARE = 0.4;
    constexpr double OVAL_RX = 1.45, OVAL_RY = 0.7;
    constexpr double WAVE_LEN = 3.6, WAVE_AMP = 0.42, WAVE_WAVES = 1.5, WAVE_HALF = 0.28;
    constexpr int WAVE_SAMPLES = 9;
    constexpr double TRI_TIP = 1.7, TRI_BASE = 0.85, TRI_HALF = 1.0;
    constexpr double STREAK_HEAD = 1.0, STREAK_HEAD_HALF = 0.42, STREAK_TAIL = 2.6, STREAK_TAIL_HALF = 0.1;
  }  // namespace shape
  GrainShape grainShape(ParticleStyle s, double w);
  double headingOf(double dx, double dy, bool fromFar);
  QPolygonF shapePolygon(GrainShape s, const QPointF& at, double r, double a);

  // drawEllipse rasterises every grain from scratch (~9ms for 4000 at 2x); a blit is ~18x cheaper
  // (measured), so a grain is drawn once per (colour, radius, half-pixel phase, heading) and copied.
  class MoteSprites {
   public:
    static constexpr double RADIUS_STEP = 0.25;   // logical px between cached radii
    static constexpr int MAX_RADIUS_STEPS = 63;    // 15.75px — far past any grain
    // The colour axis is unbounded (a theme swap walks a gradient). Over the cap the whole
    // set is dropped and refilled: an LRU eviction scan has no place in the paint loop.
    static constexpr int MAX_CACHED = 4096;
    static constexpr int HEADING_STEPS = 24;   // 15° apart

    void draw(QPainter& p, const QPointF& at, double radius, const QColor& colour,
              GrainShape shape = GrainShape::DISC, double a = 0.0);

    int cached() const { return int(cache.size()); }

   private:
    static quint32 colourKey(const QColor& c);

    static void drawExact(QPainter& q, GrainShape shape, const QPointF& at, double r, double a);
    static double reachOf(GrainShape s);

    QImage build(double radius, int px, int py, const QColor& colour, GrainShape shape, double a) const;

    QHash<quint32, QImage> cache;
    double dpr = 0;
  };

  int frameIntervalMs(const QWidget* w);

}  // namespace stencil::support
