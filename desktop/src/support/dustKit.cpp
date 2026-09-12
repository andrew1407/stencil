#include "dustKit.hpp"

namespace stencil::support {


  // The style's touch on one grain: `p` its progress, `away` its distance from home
  // (0…1), `w` its hash, `len` its throw, `ms` the cloud's clock.
  StyleFrame styleFrame(ParticleStyle s, double p, double away, double w, double len, double ms) {
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

  int paletteIndex(double mix, int stops) {
    return std::clamp(int(std::lround(mix * (stops - 1))), 0, stops - 1);
  }

  QColor paletteStop(const QColor& accent, const QColor& shade, double mix, int stops) {
    const double t = double(paletteIndex(mix, stops)) / (stops - 1);
    return QColor(qRound(accent.red() + (shade.red() - accent.red()) * t),
                  qRound(accent.green() + (shade.green() - accent.green()) * t),
                  qRound(accent.blue() + (shade.blue() - accent.blue()) * t));
  }

  int tintOf(double w) {
    const double pick = fract(w * 13.73 + 0.41);
    if (pick >= kTintShare) return -1;
    return std::min(kTintStops - 1, int(std::floor(pick / kTintShare * kTintStops)));
  }


  // `c` blended `k` of the way to a grey level (0 black, 255 white).
  QColor towards(const QColor& c, int to, double k) {
    return QColor(qRound(c.red() + (to - c.red()) * k), qRound(c.green() + (to - c.green()) * k),
                  qRound(c.blue() + (to - c.blue()) * k));
  }


  // Tints 0 and 4 follow the theme (browser css/theme.css --dust-ink / --dust-accent-alt):
  // a white speck cannot be seen on a pale surface, nor a deep accent one on a dark surface.
  QColor tintColour(const QColor& accent, int tint, bool dark) {
    if (tint == 0) return dark ? QColor(255, 255, 255) : QColor(0x1f, 0x1f, 0x1f);
    if (tint == 1) return QColor(180, 180, 180);   // #b4b4b4
    if (tint == 2) return QColor(110, 110, 110);   // #6e6e6e
    if (tint == 3) return towards(accent, 255, 1.0 - kTintAccentShare);
    return dark ? towards(accent, 255, 1.0 - kTintPaleShare) : towards(accent, 0, 1.0 - kTintAccentShare);
  }


  // The colour a grain is painted, once its tint is known — fixed for its whole flight, so
  // a caller with a per-grain cache (disintegrateOverlay.hpp tints_) passes the tint in.
  QColor tintedStop(const QColor& accent, const QColor& shade, double mix, int tint, bool dark) {
    return tint < 0 ? paletteStop(accent, shade, mix) : tintColour(accent, tint, dark);
  }


  // cubic-bezier(x1, y1, x2, y2) at time t: solve x(u) = t by bisection (monotonic in
  // x), then read y(u). The browser's bezierY (motion.js), op for op.
  double bezierY(double t, double x1, double y1, double x2, double y2) {
    double lo = 0.0, hi = 1.0, u = t;
    for (int i = 0; i < 24; i++) {
      u = 0.5 * (lo + hi);
      const double x = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
      if (x < t) lo = u; else hi = u;
    }
    return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
  }

  EaseLut::EaseLut(double x1, double y1, double x2, double y2) {
    for (int i = 0; i <= kSteps; i++) curve_[i] = bezierY(double(i) / kSteps, x1, y1, x2, y2);
    curve_.front() = 0.0;
    curve_.back() = 1.0;
  }

  // Qt's animation timer ticks every 16ms whatever the screen does; a browser's motes
  // ride the compositor at the display's own rate. A cloud ticks on this instead: one
  // frame per refresh of the screen it is on, floored at 4ms.
  int frameIntervalMs(const QWidget* w) {
    const QScreen* s = w ? w->screen() : nullptr;
    const double hz = s ? s->refreshRate() : 60.0;
    if (!(hz > 1.0)) return 16;
    return std::clamp(int(std::floor(1000.0 / hz)), 4, 16);
  }
}  // namespace stencil::support
