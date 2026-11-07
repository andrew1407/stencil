#pragma once
// The aqua capsule fills, stop for stop with browser css/aqua/tokens.css --aq-glass and --aq-gel:
// a lit cap, a hard waterline at 49–51%, the foot glowing back up. One source for the overlay
// sheet's gradients and every painter that draws a capsule. Header-only.
#include <QColor>
#include <QBrush>   // QGradientStops
#include <QString>

#include <cmath>

namespace stencil::support {

  // CSS color-mix(in srgb, a share, b), each channel rounded.
  inline QColor aquaMix(const QColor& a, double share, const QColor& b) {
    const auto ch = [share](int x, int y) { return static_cast<int>(std::lround(x * share + y * (1 - share))); };
    return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
  }

  inline QGradientStops aquaGlassStops(const QColor& top, const QColor& bottom) {
    return {{0.0, aquaMix(top, 0.7, QColor(Qt::white))}, {0.49, top}, {0.51, bottom},
            {0.75, aquaMix(top, 0.6, bottom)}, {1.0, top}};
  }

  inline QGradientStops aquaGelStops(const QColor& hi, const QColor& top, const QColor& bottom) {
    return {{0.0, hi}, {0.25, aquaMix(top, 0.7, hi)}, {0.49, top}, {0.51, bottom}, {0.82, top}, {1.0, hi}};
  }

  // The window's red close light (browser css/aqua/windows.css .app-modal-close): a lit cap over the red.
  inline QString aquaCloseLightQss(const QColor& red) {
    return QStringLiteral("qradialgradient(cx:0.5, cy:0.3, radius:0.7, fx:0.5, fy:0.3, stop:0 #ffd8d0, stop:0.26 #ffd8d0, stop:0.79 %1, stop:1 %1)")
        .arg(red.name());
  }

  // The same stops as a top-to-bottom QSS qlineargradient(...).
  inline QString aquaQssGradient(const QGradientStops& stops) {
    QString out = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1");
    for (const auto& [at, color] : stops) out += QStringLiteral(", stop:%1 %2").arg(at).arg(color.name());
    return out + ')';
  }

}  // namespace stencil::support
