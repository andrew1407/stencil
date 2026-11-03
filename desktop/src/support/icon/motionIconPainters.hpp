#pragma once
// The motion modes' glyphs as QPainter strokes, one per mode in the browser's 16-unit SVG box
// (ui/motion/icons.js), each `t` 0..1 through its `.mm-*` hover keyframes. Private to motionIcons.cpp.
#include "motionIcons.hpp"
#include "../genie/genieWarp.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace stencil::support::mmpaint {

  // Each glyph in the 16-unit box with the pen already set; `t` 0..1 through its keyframes.
  inline void paintNone(QPainter& p, const QColor&, double t) {
    p.drawEllipse(QPointF(8, 8), 5.6, 5.6);
    // Browser stroke-dashoffset 11.4 → 0.
    const double k = mmEaseOut(t);
    const QPointF a(12.05, 3.95), b(3.95, 12.05);
    if (k > 0.01) p.drawLine(a, a + (b - a) * k);
  }

  // Sliding: the genie itself (browser icons.js genieGlyphPath), the warp (support/genie/genieWarp.hpp)
  // drawn at the 16-unit scale. The hover plays the window flight's own curve, OutSine from in the dock
  // (p 1) to the bent pour it rests at, GLYPH_REST_P; the dock line it pours from stays.
  inline constexpr double GLYPH_REST_P = 0.3;
  inline void paintSlide(QPainter& p, const QColor&, double t) {
    const QRectF win(3, 2.5, 10, 8), dock(10, 13.2, 2.4, 1.2);
    const double at = 1.0 - (1.0 - GLYPH_REST_P) * std::sin(t * M_PI / 2);
    p.drawPolygon(QPolygonF(genie::outline(win, dock, genie::Side::DOWN, at, 8)));
    p.drawLine(QPointF(8.2, 14.4), QPointF(14.2, 14.4));
  }

  // Transition: the arrow (browser mmShaft / mmHead), what Sliding wore before the genie.
  inline void paintTransition(QPainter& p, const QColor&, double t) {
    // Browser mmShaft (stroke-dashoffset 9.9 → 0) over the first 35%; mmHead over the last third.
    const double ks = mmEaseOut(std::clamp(t / (0.35 / 0.45), 0.0, 1.0));
    const QPointF a(4.5, 11.5), b(11.5, 4.5);
    if (ks > 0.01) p.drawLine(a, a + (b - a) * ks);
    const double kh = std::clamp((t - 0.65) / 0.35, 0.0, 1.0);
    if (kh > 0.01) {
      p.setOpacity(mmEaseOut(kh));
      QPainterPath head;
      head.moveTo(6.5, 4.5); head.lineTo(11.5, 4.5); head.lineTo(11.5, 9.5);
      p.drawPath(head);
    }
  }

  inline void paintWater(QPainter& p, const QColor& colour, double t) {
    // 0% translateY(-9) scale(.6) fade → 70% landed squashed (1.06, .94) → 100% rest.
    double dy, sx, sy, op;
    if (t < 0.7) { const double k = mmOut(t / 0.7); dy = -9 * (1 - k); sx = mmLerp(0.6, 1.06, k); sy = mmLerp(0.6, 0.94, k); op = k; }
    else { const double k = mmOut((t - 0.7) / 0.3); dy = 0; sx = mmLerp(1.06, 1, k); sy = mmLerp(0.94, 1, k); op = 1; }
    p.setOpacity(op);
    p.translate(8, 8 + dy); p.scale(sx, sy); p.translate(-8, -8);
    QPainterPath drop;
    drop.moveTo(8, 2.4);
    drop.cubicTo(8, 2.4, 3.9, 7.4, 3.9, 10.1);
    drop.arcTo(QRectF(3.9, 6.0, 8.2, 8.2), 180, 180);
    drop.cubicTo(12.1, 7.4, 8, 2.4, 8, 2.4);
    drop.closeSubpath();
    p.setPen(Qt::NoPen); p.setBrush(colour);
    p.drawPath(drop);
  }

  inline void paintFire(QPainter& p, const QColor& colour, double t) {
    // Keyframes: 0% scale(.5,.15) fade → 35% (1.05,.8) → 60% (.94,1.1) → 80% (1.04,.96) → 100% rest.
    struct K { double at, sx, sy, op; };
    const K keys[] = {{0, 0.5, 0.15, 0}, {0.35, 1.05, 0.8, 1}, {0.6, 0.94, 1.1, 1}, {0.8, 1.04, 0.96, 1}, {1, 1, 1, 1}};
    K a = keys[0], b = keys[1];
    for (int i = 1; i < 5; i++) if (t >= keys[i - 1].at) { a = keys[i - 1]; b = keys[i]; }
    const double k = b.at > a.at ? mmEaseOut((t - a.at) / (b.at - a.at)) : 1;
    p.setOpacity(mmLerp(a.op, b.op, k));
    // transform-origin 50% 100% of the flame's box (x 3.7..12.3, bottom y 13.6).
    p.translate(8, 13.6); p.scale(mmLerp(a.sx, b.sx, k), mmLerp(a.sy, b.sy, k)); p.translate(-8, -13.6);
    QPainterPath flame;
    flame.moveTo(8.2, 1.6);
    flame.cubicTo(9.4, 4.2, 12.3, 5.8, 12.3, 9.3);
    flame.arcTo(QRectF(3.7, 5.0, 8.6, 8.6), 0, -180);
    flame.cubicTo(3.7, 7.4, 5.0, 6.4, 5.6, 4.9);
    flame.cubicTo(6.4, 6.0, 6.9, 6.7, 7.4, 6.5);
    flame.cubicTo(7.0, 5.0, 7.5, 3.2, 8.2, 1.6);
    flame.closeSubpath();
    p.setPen(Qt::NoPen); p.setBrush(colour);
    p.drawPath(flame);
  }

  // Dust: five specks settling in from the top, 60ms apart over 550ms.
  inline void paintParticles(QPainter& p, const QColor& colour, double t) {
    const QPointF at[5] = {{4, 6}, {8.5, 4.2}, {12, 7}, {6.2, 10.5}, {10.4, 11.6}};
    const double r[5] = {1.3, 1.0, 1.2, 1.1, 1.35};
    p.setPen(Qt::NoPen); p.setBrush(colour);
    for (int i = 0; i < 5; i++) {
      // Browser mmDust + nth-of-type delays: 90ms apart, 825ms each, `t` over the whole 1185ms.
      const double start = i * 90 / 1185.0, span = 825 / 1185.0;
      const double k = mmOut(std::clamp((t - start) / span, 0.0, 1.0));
      p.setOpacity(k);
      p.drawEllipse(at[i] + QPointF(0, -7 * (1 - k)), r[i], r[i]);
    }
  }

}  // namespace stencil::support::mmpaint
