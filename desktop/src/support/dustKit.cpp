#include "dustKit.hpp"

namespace stencil::support {


  // `p` progress, `away` distance from home (0…1), `w` hash, `len` throw, `ms` the clock.
  StyleFrame styleFrame(ParticleStyle s, double p, double away, double w, double len, double ms) {
    using namespace style;
    StyleFrame out;
    if (s != ParticleStyle::WATER && s != ParticleStyle::FIRE) return out;
    const double env = std::sin(PI * p);
    const double phase = w * 2 * PI;
    const double sec = ms / 1000.0;
    if (s == ParticleStyle::WATER) {
      out.sy = std::min(len * WATER_SAG_SHARE, WATER_SAG_MAX_PX) * (0.6 + 0.4 * w) * env;
      out.sx = std::min(len * WATER_SWAY_SHARE, WATER_SWAY_MAX_PX) * env
             * std::sin(p * wave(WATER_SWAY_WAVES, w) * 2 * PI + phase);
      out.scale = 1 + WATER_SWELL * env;
      out.glow = 1 - WATER_SHIMMER_DEPTH * 0.5 * (1 + std::sin(sec * wave(WATER_SHIMMER_HZ, w) * 2 * PI + phase));
      out.mix = 0.5 + 0.5 * std::sin(sec * wave(WATER_GLISTEN_HZ, w) * 2 * PI + phase);
    } else {
      out.sy = -std::min(len * FIRE_LIFT_SHARE, FIRE_LIFT_MAX_PX) * (0.5 + 0.5 * w) * env;
      out.sx = std::min(len * FIRE_WAVER_SHARE, FIRE_WAVER_MAX_PX) * env
             * std::sin(p * wave(FIRE_WAVER_WAVES, w) * 2 * PI + phase);
      const double dim = 0.5 * (1 + std::sin(sec * wave(FIRE_FLICKER_HZ, w) * 2 * PI + phase));   // 0 bright … 1 dim
      out.glow = 1 - FIRE_FLICKER_DEPTH * dim;
      out.scale = 1 + FIRE_FLARE * env * (1 - dim);
      out.mix = std::clamp(FIRE_COOL_HASH * w + (1 - FIRE_COOL_HASH) * away, 0.0, 1.0);
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
    if (pick >= TINT_SHARE) return -1;
    return std::min(TINT_STOPS - 1, int(std::floor(pick / TINT_SHARE * TINT_STOPS)));
  }


  // `c` blended `k` of the way to a grey level (0 black, 255 white).
  QColor towards(const QColor& c, int to, double k) {
    return QColor(qRound(c.red() + (to - c.red()) * k), qRound(c.green() + (to - c.green()) * k),
                  qRound(c.blue() + (to - c.blue()) * k));
  }


  // browser css/theme.css --dust-ink / --dust-accent-alt: a white speck cannot be seen on
  // a pale surface, nor a deep accent one on a dark surface.
  QColor tintColour(const QColor& accent, int tint, bool dark) {
    if (tint == 0) return dark ? QColor(255, 255, 255) : QColor(0x1f, 0x1f, 0x1f);
    if (tint == 1) return QColor(180, 180, 180);   // #b4b4b4
    if (tint == 2) return QColor(110, 110, 110);   // #6e6e6e
    if (tint == 3) return towards(accent, 255, 1.0 - TINT_ACCENT_SHARE);
    return dark ? towards(accent, 255, 1.0 - TINT_PALE_SHARE) : towards(accent, 0, 1.0 - TINT_ACCENT_SHARE);
  }


  // Fixed for its whole flight, so a caller with a per-grain cache passes the tint in.
  QColor tintedStop(const QColor& accent, const QColor& shade, double mix, int tint, bool dark) {
    return tint < 0 ? paletteStop(accent, shade, mix) : tintColour(accent, tint, dark);
  }


  // cubic-bezier(x1,y1,x2,y2) at time t: solve x(u)=t by bisection, then read y(u). Browser bezierY.
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
    for (int i = 0; i <= STEPS; i++) curve_[i] = bezierY(double(i) / STEPS, x1, y1, x2, y2);
    curve_.front() = 0.0;
    curve_.back() = 1.0;
  }

  // A browser's motes ride the compositor at the display's rate; this ticks one frame
  // per refresh instead of Qt's fixed 16ms, floored at 4ms.
  int frameIntervalMs(const QWidget* w) {
    const QScreen* s = w ? w->screen() : nullptr;
    const double hz = s ? s->refreshRate() : 60.0;
    if (!(hz > 1.0)) return 16;
    return std::clamp(int(std::floor(1000.0 / hz)), 4, 16);
  }
}  // namespace stencil::support
