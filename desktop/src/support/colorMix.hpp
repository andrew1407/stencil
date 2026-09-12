#pragma once
#include <QColor>

// The app's two colour mixers, NOT interchangeable (they disagree by one 8-bit step on
// real pairs, e.g. #aaaaaa over #2d2d2d at 0.7 → 133 vs 132); both results are pinned.
namespace stencil::gui {

  // CSS color-mix(in srgb, a (1-t), b t) in float. Opaque: alpha is not interpolated.
  inline QColor mixSrgb(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
  }

  // `a` over `b` at `t`, rounded per 8-bit channel — for places a translucent colour is
  // not an option (a rasterised icon's cache key, a painted tail with no known backdrop).
  inline QColor blendColors(const QColor& a, const QColor& b, double t) {
    return QColor(qRound(a.red() * t + b.red() * (1 - t)),
                  qRound(a.green() * t + b.green() * (1 - t)),
                  qRound(a.blue() * t + b.blue() * (1 - t)));
  }

}
