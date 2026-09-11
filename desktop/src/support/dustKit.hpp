#pragma once
// The bits every desktop cloud shares (support/disintegrateOverlay.hpp,
// support/themeSwapOverlay.hpp): the browser's cubic-bezier easings as lookup tables,
// a sprite cache that blits a round grain instead of rasterising one, and the clock a
// cloud ticks on — the screen's own refresh rate, not Qt's 60Hz animation timer.
//
// Header-only, Q_OBJECT-free.
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

  // Particle styles (browser dustCloud.js styleFrame — keep the numbers in step)
  // A style is a touch laid over ANY flight, gone at both ends: an offset (px), a size
  // multiplier, a brightness (`glow`) and `mix`, where between the main colour (0) and its
  // shade (1) the grain is painted. Dust is the identity.
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
  // The style's touch on one grain: `p` its progress, `away` its distance from home
  // (0…1), `w` its hash, `len` its throw, `ms` the cloud's clock.
  inline StyleFrame styleFrame(ParticleStyle s, double p, double away, double w, double len, double ms) {
    using namespace style;
    StyleFrame out;
    if (s != ParticleStyle::Water && s != ParticleStyle::Fire) return out;
    const double env = std::sin(kPi * p);
    const double phase = w * 2 * kPi;
    const double sec = ms / 1000.0;
    if (s == ParticleStyle::Water) {
      out.sy = std::min(len * kWaterSagShare, kWaterSagMaxPx) * (0.6 + 0.4 * w) * env;
      out.sx = std::min(len * kWaterSwayShare, kWaterSwayMaxPx) * env
             * std::sin(p * wave(kWaterSwayWaves, w) * 2 * kPi + phase);
      out.scale = 1 + kWaterSwell * env;
      out.glow = 1 - kWaterShimmerDepth * 0.5 * (1 + std::sin(sec * wave(kWaterShimmerHz, w) * 2 * kPi + phase));
      out.mix = 0.5 + 0.5 * std::sin(sec * wave(kWaterGlistenHz, w) * 2 * kPi + phase);
    } else {
      out.sy = -std::min(len * kFireLiftShare, kFireLiftMaxPx) * (0.5 + 0.5 * w) * env;
      out.sx = std::min(len * kFireWaverShare, kFireWaverMaxPx) * env
             * std::sin(p * wave(kFireWaverWaves, w) * 2 * kPi + phase);
      const double dim = 0.5 * (1 + std::sin(sec * wave(kFireFlickerHz, w) * 2 * kPi + phase));   // 0 bright … 1 dim
      out.glow = 1 - kFireFlickerDepth * dim;
      out.scale = 1 + kFireFlare * env * (1 - dim);
      out.mix = std::clamp(kFireCoolHash * w + (1 - kFireCoolHash) * away, 0.0, 1.0);
    }
    return out;
  }
  // A DUST grain's mix, fixed for its flight (browser dustCloud.js dustMix): plain grains
  // spread from the main colour to halfway by their hash, glints wear the shade.
  constexpr double kDustMixSpread = 0.5;
  inline double dustMix(double w, bool glint) { return glint ? 1.0 : w * kDustMixSpread; }
  inline double fract(double v) { return v - std::floor(v); }
  // A cloud's palette: this many even mixes from the main colour to its shade
  // (browser paletteCss / paletteIndex — CSS color-mix in srgb is this same straight mix).
  constexpr int kPaletteStops = 6;
  inline int paletteIndex(double mix, int stops = kPaletteStops) {
    return std::clamp(int(std::lround(mix * (stops - 1))), 0, stops - 1);
  }
  inline QColor paletteStop(const QColor& accent, const QColor& shade, double mix, int stops = kPaletteStops) {
    const double t = double(paletteIndex(mix, stops)) / (stops - 1);
    return QColor(qRound(accent.red() + (shade.red() - accent.red()) * t),
                  qRound(accent.green() + (shade.green() - accent.green()) * t),
                  qRound(accent.blue() + (shade.blue() - accent.blue()) * t));
  }
  // Two grains in three ride that ramp; the rest wear a TINT off their own hash — a neutral
  // spark, two greys and two accents. Browser twin: dustCloud.js TINT_CSS / tintOf; the
  // share and the mixes are the contract.
  constexpr double kTintShare = 0.34;
  constexpr int kTintStops = 5;
  constexpr double kTintAccentShare = 0.55;   // …of the accent in the pale and the deep one
  constexpr double kTintPaleShare = 0.30;     // …and in the pale one the dark theme swaps in
  inline int tintOf(double w) {
    const double pick = fract(w * 13.73 + 0.41);
    if (pick >= kTintShare) return -1;
    return std::min(kTintStops - 1, int(std::floor(pick / kTintShare * kTintStops)));
  }
  // `c` blended `k` of the way to a grey level (0 black, 255 white).
  inline QColor towards(const QColor& c, int to, double k) {
    return QColor(qRound(c.red() + (to - c.red()) * k), qRound(c.green() + (to - c.green()) * k),
                  qRound(c.blue() + (to - c.blue()) * k));
  }
  // Tints 0 and 4 follow the theme (browser css/theme.css --dust-ink / --dust-accent-alt):
  // a white speck cannot be seen on a pale surface, nor a deep accent one on a dark surface.
  inline QColor tintColour(const QColor& accent, int tint, bool dark) {
    if (tint == 0) return dark ? QColor(255, 255, 255) : QColor(0x1f, 0x1f, 0x1f);
    if (tint == 1) return QColor(180, 180, 180);   // #b4b4b4
    if (tint == 2) return QColor(110, 110, 110);   // #6e6e6e
    if (tint == 3) return towards(accent, 255, 1.0 - kTintAccentShare);
    return dark ? towards(accent, 255, 1.0 - kTintPaleShare) : towards(accent, 0, 1.0 - kTintAccentShare);
  }
  // The colour a grain is painted, once its tint is known — fixed for its whole flight, so
  // a caller with a per-grain cache (disintegrateOverlay.hpp tints_) passes the tint in.
  inline QColor tintedStop(const QColor& accent, const QColor& shade, double mix, int tint, bool dark) {
    return tint < 0 ? paletteStop(accent, shade, mix) : tintColour(accent, tint, dark);
  }

  // cubic-bezier(x1, y1, x2, y2) at time t: solve x(u) = t by bisection (monotonic in
  // x), then read y(u). The browser's bezierY (motion.js), op for op.
  inline double bezierY(double t, double x1, double y1, double x2, double y2) {
    double lo = 0.0, hi = 1.0, u = t;
    for (int i = 0; i < 24; i++) {
      u = 0.5 * (lo + hi);
      const double x = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
      if (x < t) lo = u; else hi = u;
    }
    return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
  }

  // A curve sampled once into 256 steps, both ends pinned exactly (the solver only
  // bisects to within a hair of 0 and 1, and that hair leaves a spent grain a fraction
  // lit). Thousands of grains read it per frame; solving per read was the frame.
  class EaseLut {
   public:
    static constexpr int kSteps = 256;
    EaseLut(double x1, double y1, double x2, double y2) {
      for (int i = 0; i <= kSteps; i++) curve_[i] = bezierY(double(i) / kSteps, x1, y1, x2, y2);
      curve_.front() = 0.0;
      curve_.back() = 1.0;
    }
    double at(double t) const {
      return curve_[std::clamp(int(std::lround(t * kSteps)), 0, kSteps)];
    }
   private:
    std::array<double, kSteps + 1> curve_{};
  };

  // Grain shapes (browser dustCloud.js grainShape / shapePolygon / addGrainPath)
  // Dust is a round speck; water ovals and short wave lines; fire triangles and streaking
  // sparks, each lying along its heading. Geometry in radii — keep the browser's numbers.
  enum class GrainShape { Disc, Oval, Wave, Triangle, Streak };
  // A styled grain is bigger and blits more pixels, so a screen-sized cloud grids at this
  // many times the cell under water and fire (browser dustCloud.js STYLED_CELL_SCALE).
  constexpr double kStyledCellScale = 1.4;
  namespace shape {
    constexpr double kWaterWaveShare = 0.3, kFireStreakShare = 0.4;
    constexpr double kOvalRx = 1.45, kOvalRy = 0.7;
    constexpr double kWaveLen = 3.6, kWaveAmp = 0.42, kWaveWaves = 1.5, kWaveHalf = 0.28;
    constexpr int kWaveSamples = 9;
    constexpr double kTriTip = 1.7, kTriBase = 0.85, kTriHalf = 1.0;
    constexpr double kStreakHead = 1.0, kStreakHeadHalf = 0.42, kStreakTail = 2.6, kStreakTailHalf = 0.1;
  }  // namespace shape
  inline GrainShape grainShape(ParticleStyle s, double w) {
    const double pick = fract(w * 7.31 + 0.17);
    if (s == ParticleStyle::Water) return pick < shape::kWaterWaveShare ? GrainShape::Wave : GrainShape::Oval;
    if (s == ParticleStyle::Fire) return pick < shape::kFireStreakShare ? GrainShape::Streak : GrainShape::Triangle;
    return GrainShape::Disc;
  }
  // The heading of a grain thrown `dx, dy` from home; a gather flies the other way.
  inline double headingOf(double dx, double dy, bool fromFar) {
    return std::atan2(dy, dx) + (fromFar ? style::kPi : 0.0);
  }
  // A polygon shape at `at`, radius r, heading a (Disc and Oval are drawn as ellipses).
  inline QPolygonF shapePolygon(GrainShape s, const QPointF& at, double r, double a) {
    using namespace shape;
    QPolygonF out;
    const double c = std::cos(a), sn = std::sin(a);
    const auto put = [&](double u, double v) { out << QPointF(at.x() + u * c - v * sn, at.y() + u * sn + v * c); };
    if (s == GrainShape::Triangle) {
      put(kTriTip * r, 0); put(-kTriBase * r, kTriHalf * r); put(-kTriBase * r, -kTriHalf * r);
    } else if (s == GrainShape::Streak) {
      put(kStreakHead * r, kStreakHeadHalf * r); put(-kStreakTail * r, kStreakTailHalf * r);
      put(-kStreakTail * r, -kStreakTailHalf * r); put(kStreakHead * r, -kStreakHeadHalf * r);
    } else if (s == GrainShape::Wave) {
      for (int i = 0; i < kWaveSamples; i++) {
        const double k = double(i) / (kWaveSamples - 1);
        put((k - 0.5) * kWaveLen * r, std::sin(k * kWaveWaves * 2 * style::kPi) * kWaveAmp * r + kWaveHalf * r);
      }
      for (int i = kWaveSamples - 1; i >= 0; i--) {
        const double k = double(i) / (kWaveSamples - 1);
        put((k - 0.5) * kWaveLen * r, std::sin(k * kWaveWaves * 2 * style::kPi) * kWaveAmp * r - kWaveHalf * r);
      }
    }
    return out;
  }

  // A grain is an antialiased disc a few pixels across, and QPainter::drawEllipse
  // rasterises every one from scratch: ~9ms for a dialog's 4000 at 2x, which is why the
  // desktop's clouds ticked at 60Hz and still lagged. Blitting a pre-drawn disc is ~18x
  // cheaper (measured), so a grain is drawn once per (colour, radius, half-pixel phase)
  // and then only copied; its alpha rides the painter's opacity. Positions land on half
  // device pixels, which at any DPI is finer than the eye tracks a moving speck at.
  class MoteSprites {
   public:
    static constexpr double kRadiusStep = 0.25;   // logical px between cached radii
    static constexpr int kMaxRadiusSteps = 63;    // 15.75px — far past any grain
    // Bound: shapes × headings × colours × radii is unbounded in the colour axis, and a
    // theme swap walks a gradient of them. Over the cap the whole set is dropped and
    // refilled from what is actually on screen — an LRU's eviction scan has no business
    // in a 60 Hz paint loop, and never evicting left every later grain on the slow path.
    static constexpr int kMaxCached = 4096;
    // A shaped grain lies along its heading; the cache holds it at this many headings
    // round the clock (15° apart — finer than the eye tracks a moving speck at).
    static constexpr int kHeadingSteps = 24;

    // Paint one grain — a disc, or any GrainShape at heading `a`. Antialiasing must be OFF
    // on `p` (the sprite carries its own), or the raster engine leaves its 1:1 blit path.
    void draw(QPainter& p, const QPointF& at, double radius, const QColor& colour,
              GrainShape shape = GrainShape::Disc, double a = 0.0) {
      const double alpha = colour.alphaF();
      if (radius < 0.2 || alpha <= 1.0 / 255) return;
      const double dpr = p.device()->devicePixelRatio();
      if (dpr != dpr_) { cache_.clear(); dpr_ = dpr; }
      // A shaped grain is cached at half the radius resolution (every miss rasterises a
      // polygon, and a quarter-pixel of size is invisible on a moving drop).
      const bool disc = shape == GrainShape::Disc;
      const double step = disc ? kRadiusStep : kRadiusStep * 2;
      const int rb = std::min(kMaxRadiusSteps, std::max(1, int(std::lround(radius / step))));
      const double xd = at.x() * dpr, yd = at.y() * dpr;
      const double xf = std::floor(xd), yf = std::floor(yd);
      const int px = xd - xf >= 0.5 ? 1 : 0, py = yd - yf >= 0.5 ? 1 : 0;
      const int hs = disc ? 0
          : ((int(std::lround(a / (2 * style::kPi) * kHeadingSteps)) % kHeadingSteps) + kHeadingSteps) % kHeadingSteps;
      const quint32 key = (colourKey(colour) << 16) | quint32(rb << 10) | quint32(int(shape) << 7)
                        | quint32(hs << 2) | quint32(py << 1) | quint32(px);
      auto it = cache_.find(key);
      if (it == cache_.end()) {
        if (cache_.size() >= kMaxCached) cache_.clear();
        it = cache_.insert(key, build(rb * step, px, py, colour, shape, hs * 2 * style::kPi / kHeadingSteps));
      }
      const QImage& img = it.value();
      const int half = img.width() / 2;   // device px; the disc is centred at half (+ phase)
      p.setOpacity(alpha);
      p.drawImage(QPointF((xf - half) / dpr, (yf - half) / dpr), img);
    }

    int cached() const { return int(cache_.size()); }

   private:
    // 5 bits a channel: a grain a 32nd of a step off its neighbour's colour shares its disc.
    static quint32 colourKey(const QColor& c) {
      return quint32((c.red() >> 3) << 10 | (c.green() >> 3) << 5 | (c.blue() >> 3));
    }

    // The exact shape, drawn with the painter's current brush (the cache's fallback, and
    // what every sprite is rasterised from).
    static void drawExact(QPainter& q, GrainShape shape, const QPointF& at, double r, double a) {
      if (shape == GrainShape::Disc) { q.drawEllipse(at, r, r); return; }
      if (shape == GrainShape::Oval) {
        q.save();
        q.translate(at);
        q.rotate(a * 180.0 / style::kPi);
        q.drawEllipse(QPointF(0, 0), shape::kOvalRx * r, shape::kOvalRy * r);
        q.restore();
        return;
      }
      q.drawPolygon(shapePolygon(shape, at, r, a));
    }
    // How far a shape reaches from its centre, in radii — the sprite is only as big as
    // that (a blit costs by area, and a spark's tail is the longest reach).
    static double reachOf(GrainShape s) {
      using namespace shape;
      switch (s) {
        case GrainShape::Oval: return kOvalRx;
        case GrainShape::Wave: return std::hypot(kWaveLen / 2, kWaveAmp + kWaveHalf);
        case GrainShape::Triangle: return std::max(kTriTip, std::hypot(kTriBase, kTriHalf));
        case GrainShape::Streak: return kStreakTail;
        case GrainShape::Disc: break;
      }
      return 1.0;
    }

    QImage build(double radius, int px, int py, const QColor& colour, GrainShape shape, double a) const {
      const double r = radius * dpr_;
      const int half = int(std::ceil(r * reachOf(shape))) + 1;
      QImage img(half * 2, half * 2, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter q(&img);
      q.setRenderHint(QPainter::Antialiasing, true);
      q.setPen(Qt::NoPen);
      QColor solid = colour;
      solid.setAlphaF(1.0);
      q.setBrush(solid);
      drawExact(q, shape, QPointF(half + px * 0.5, half + py * 0.5), r, a);
      q.end();
      img.setDevicePixelRatio(dpr_);   // logical size = device / dpr, so it blits 1:1
      return img;
    }

    QHash<quint32, QImage> cache_;
    double dpr_ = 0;
  };

  // Qt's animation timer ticks every 16ms whatever the screen does; a browser's motes
  // ride the compositor at the display's own rate. A cloud ticks on this instead: one
  // frame per refresh of the screen it is on, floored at 4ms.
  inline int frameIntervalMs(const QWidget* w) {
    const QScreen* s = w ? w->screen() : nullptr;
    const double hz = s ? s->refreshRate() : 60.0;
    if (!(hz > 1.0)) return 16;
    return std::clamp(int(std::floor(1000.0 / hz)), 4, 16);
  }

}  // namespace stencil::support
