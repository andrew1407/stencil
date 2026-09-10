#pragma once
#include <QColor>

// The app's two colour mixers. They are NOT interchangeable: on real palette pairs
// they disagree by one 8-bit step (e.g. #aaaaaa over #2d2d2d at 0.7 → 133 vs 132),
// and both results are pinned — the QSS hexes and the painted cards ride on them.
// Named side by side so the split reads as a decision, not as a stray second copy.
namespace stencil::gui {

  // CSS color-mix(in srgb, a (1-t), b t) in float — the accent shade, the glows,
  // the row washes and every animated blend. Opaque: alpha is not interpolated.
  inline QColor mixSrgb(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
  }

  // `a` over `b` at `t` opacity, rounded per 8-bit channel — for the places a
  // translucent colour is not an option (a rasterised icon whose cache is keyed on an
  // alpha-less name, a painted tail with no known backdrop, a QSS hex).
  inline QColor blendColors(const QColor& a, const QColor& b, double t) {
    return QColor(qRound(a.red() * t + b.red() * (1 - t)),
                  qRound(a.green() * t + b.green() * (1 - t)),
                  qRound(a.blue() * t + b.blue() * (1 - t)));
  }

}
