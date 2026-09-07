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

#include <algorithm>
#include <array>
#include <cmath>

namespace stencil::support {

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

  // ── The sprite cache ──
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
    static constexpr int kMaxCached = 8000;       // a photograph's worth of colours, then drawEllipse

    // Paint one grain. Antialiasing must be OFF on `p` (the sprite carries its own):
    // the raster engine only takes its 1:1 blit fast path for an aliased painter.
    void draw(QPainter& p, const QPointF& at, double radius, const QColor& colour) {
      const double alpha = colour.alphaF();
      if (radius < 0.2 || alpha <= 1.0 / 255) return;
      const double dpr = p.device()->devicePixelRatio();
      if (dpr != dpr_) { cache_.clear(); dpr_ = dpr; }
      const int rb = std::min(kMaxRadiusSteps, std::max(1, int(std::lround(radius / kRadiusStep))));
      const double xd = at.x() * dpr, yd = at.y() * dpr;
      const double xf = std::floor(xd), yf = std::floor(yd);
      const int px = xd - xf >= 0.5 ? 1 : 0, py = yd - yf >= 0.5 ? 1 : 0;
      const quint32 key = (colourKey(colour) << 8) | quint32(rb << 2) | quint32(py << 1) | quint32(px);
      auto it = cache_.find(key);
      if (it == cache_.end()) {
        if (cache_.size() >= kMaxCached) {   // out of room: the slow, exact way
          p.save();
          p.setRenderHint(QPainter::Antialiasing, true);
          p.setPen(Qt::NoPen);
          p.setBrush(colour);
          p.drawEllipse(at, radius, radius);
          p.restore();
          return;
        }
        it = cache_.insert(key, build(rb * kRadiusStep, px, py, colour));
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

    QImage build(double radius, int px, int py, const QColor& colour) const {
      const double r = radius * dpr_;
      const int half = int(std::ceil(r)) + 1;
      QImage img(half * 2, half * 2, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter q(&img);
      q.setRenderHint(QPainter::Antialiasing, true);
      q.setPen(Qt::NoPen);
      QColor solid = colour;
      solid.setAlphaF(1.0);
      q.setBrush(solid);
      q.drawEllipse(QPointF(half + px * 0.5, half + py * 0.5), r, r);
      q.end();
      img.setDevicePixelRatio(dpr_);   // logical size = device / dpr, so it blits 1:1
      return img;
    }

    QHash<quint32, QImage> cache_;
    double dpr_ = 0;
  };

  // ── The frame clock ──
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
