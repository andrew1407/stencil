#pragma once
// The genie warp: a window's snapshot cut into bands that bend into a funnel toward the icon it
// belongs to and are sucked into it, the near edge first, the way a macOS window minimises. Pure
// geometry; GenieGhost.hpp paints it. Browser port: js/ui/motion/genie/warp.js.
#include <QRectF>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace stencil::support::genie {

  // The direction the bands flow in, toward the icon.
  enum class Side { DOWN, UP, RIGHT, LEFT };

  struct Strip {
    QRectF target;   // where the band lands, in the coordinates the rects were given in
    QRectF source;   // the band, as fractions (0–1) of the snapshot
  };

  // The near edge leads and the far edge trails it by LAG of the flight, so the window stretches as it
  // pours; over the first PINCH_END the near bands narrow by BEND of the way to the icon's width.
  inline constexpr double LAG = 0.55;
  inline constexpr double PINCH_END = 0.40;
  inline constexpr double BEND = 0.85;
  // Snapshot px per strip at most, and the strip count's floor and ceiling.
  inline constexpr double STRIP_PX = 3.0;
  inline constexpr int MIN_STRIPS = 24;
  inline constexpr int MAX_STRIPS = 240;

  // A target past this AREA (a MAX_ICON_PX square) is no icon but a region (the canvas a closing window
  // pours into): the window flows to a point of SINK_PX at its centre instead of spreading over it. An
  // edge strip of any length (a selector's underside) stays as it is.
  inline constexpr double MAX_ICON_PX = 96;
  inline constexpr double SINK_PX = 24;
  inline QRectF sinkOf(const QRectF& icon) {
    if (icon.width() * icon.height() <= MAX_ICON_PX * MAX_ICON_PX) return icon;
    QRectF sink(0, 0, SINK_PX, SINK_PX);
    sink.moveCenter(icon.center());
    return sink;
  }

  inline double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
  inline double smooth(double v) { v = clamp01(v); return v * v * (3.0 - 2.0 * v); }

  // The edge the icon lies beyond, the widest gap winning; an icon over the window flows the way its
  // centre lies from the window's.
  inline Side sideOf(const QRectF& win, const QRectF& icon) {
    const QPointF c = icon.center();
    const double gaps[4] = {c.y() - win.bottom(), win.top() - c.y(), c.x() - win.right(), win.left() - c.x()};
    int best = -1;
    for (int i = 0; i < 4; ++i)
      if (gaps[i] > 0 && (best < 0 || gaps[i] > gaps[best])) best = i;
    if (best >= 0) return static_cast<Side>(best);
    const QPointF d = c - win.center();
    if (std::abs(d.y()) >= std::abs(d.x())) return d.y() >= 0 ? Side::DOWN : Side::UP;
    return d.x() >= 0 ? Side::RIGHT : Side::LEFT;
  }

  // A rect in canonical form: `along` runs toward the icon (first = far edge), `across` beside it.
  struct Span { double a0, a1, c0, c1; };
  inline Span canon(const QRectF& r, Side s) {
    switch (s) {
      case Side::DOWN: return {r.top(), r.bottom(), r.left(), r.right()};
      case Side::UP: return {-r.bottom(), -r.top(), r.left(), r.right()};
      case Side::RIGHT: return {r.left(), r.right(), r.top(), r.bottom()};
      case Side::LEFT: return {-r.right(), -r.left(), r.top(), r.bottom()};
    }
    return {};
  }
  inline QRectF uncanon(double a0, double a1, double c0, double c1, Side s) {
    switch (s) {
      case Side::DOWN: return QRectF(QPointF(c0, a0), QPointF(c1, a1));
      case Side::UP: return QRectF(QPointF(c0, -a1), QPointF(c1, -a0));
      case Side::RIGHT: return QRectF(QPointF(a0, c0), QPointF(a1, c1));
      case Side::LEFT: return QRectF(QPointF(-a1, c0), QPointF(-a0, c1));
    }
    return {};
  }
  // A band's source fractions: `v` runs from the window's far edge to its near one.
  inline QRectF sourceBand(double v0, double v1, Side s) {
    switch (s) {
      case Side::DOWN: return QRectF(0, v0, 1, v1 - v0);
      case Side::UP: return QRectF(0, 1 - v1, 1, v1 - v0);
      case Side::RIGHT: return QRectF(v0, 0, v1 - v0, 1);
      case Side::LEFT: return QRectF(1 - v1, 0, v1 - v0, 1);
    }
    return {};
  }

  inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
  // How far source row `v` (0 = far edge, 1 = near edge) has travelled at progress `p`.
  inline double travel(double v, double p) { return smooth(p * (1.0 + LAG) - LAG * (1.0 - v)); }

  // The bands at progress `p`: 0 = the window at rest, 1 = shrunk onto the icon. Into `out`, whose
  // capacity a per-frame caller keeps.
  inline void warpInto(QVector<Strip>& out, const QRectF& win, const QRectF& icon, Side side, double p) {
    out.clear();
    const Span w = canon(win, side), i = canon(icon, side);
    const double len = w.a1 - w.a0;
    if (len <= 0 || w.c1 <= w.c0) return;
    const double pinch = smooth(p / PINCH_END);
    const int n = std::clamp(int(len / STRIP_PX), MIN_STRIPS, MAX_STRIPS);
    out.reserve(n);
    const auto along = [&](double v) {
      return lerp(w.a0 + v * len, i.a0 + v * (i.a1 - i.a0), travel(v, p));
    };
    double a0 = along(0.0);
    for (int k = 0; k < n; ++k) {
      const double v0 = double(k) / n, v1 = double(k + 1) / n, vm = (v0 + v1) / 2;
      const double a1 = along(v1);
      const double f = std::max(travel(vm, p), pinch * BEND * smooth(vm));
      out.push_back({uncanon(a0, a1, lerp(w.c0, i.c0, f), lerp(w.c1, i.c1, f), side), sourceBand(v0, v1, side)});
      a0 = a1;
    }
  }
  inline QVector<Strip> warp(const QRectF& win, const QRectF& icon, Side side, double p) {
    QVector<Strip> out;
    warpInto(out, win, icon, side, p);
    return out;
  }

  // The warp's silhouette at `p`: rows + 1 points down one side and back up the other (browser warp.js
  // genieOutline), a fixed count — the theme swap's Sliding hole (dust/ThemeSwapOverlayPaint.cpp).
  inline QVector<QPointF> outline(const QRectF& win, const QRectF& icon, Side side, double p, int rows = 24) {
    const Span w = canon(win, side), i = canon(icon, side);
    const double len = w.a1 - w.a0, pinch = smooth(p / PINCH_END);
    QVector<QPointF> out(2 * (rows + 1));
    for (int k = 0; k <= rows; ++k) {
      const double v = double(k) / rows;
      const double a = lerp(w.a0 + v * len, i.a0 + v * (i.a1 - i.a0), travel(v, p));
      const double f = std::max(travel(v, p), pinch * BEND * smooth(v));
      const QRectF r = uncanon(a, a, lerp(w.c0, i.c0, f), lerp(w.c1, i.c1, f), side);
      out[k] = r.topLeft();
      out[out.size() - 1 - k] = r.bottomRight();
    }
    return out;
  }

}  // namespace stencil::support::genie
