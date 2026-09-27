#pragma once
// The canvas's highlight and divider metrics (browser/js/config/constants.json FOCUS_RING,
// HOVER_RING, SELECT_GLOW and COMPARE_DIVIDER through the qrc), read once; the browser's
// core/line/render.js and core/draw/renderer.js read the same keys.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace stencil::gui::markMetrics {

  // Image px unless named otherwise. A build without the qrc; tests/canvas/scene/markMetrics holds
  // these equal to the table.
  struct Table {
    double focusGapPx = 6.0;          // the focused point's ring sits this far outside its radius
    double focusWidthPx = 3.0;
    double focusBlurPx = 12.0;        // the ring's glow, a canvas shadowBlur (sigma = blur / 2)
    double focusGlowAlpha = 0.9;
    double hoverGapPx = 4.0;
    double hoverWidthPx = 1.8;
    double hoverAlpha = 0.55;
    double pointGlowGapPx = 4.0;      // the selection disc under a point
    double pointGlowAlpha = 0.5;
    double lineGlowPadPx = 8.0;       // added to the stroke width
    double lineGlowAlpha = 0.6;
    double lineHoverPadPx = 6.0;
    double lineHoverAlpha = 0.35;
    double dividerLineWidthPx = 2.0;  // screen px
    double dividerKnobRadiusPx = 7.0;
    double dividerGrabSlackPx = 8.0;  // screen px either side the divider takes a press
  };

  inline const Table& table() {
    static const Table t = [] {
      Table out;
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      auto num = [&root](const char* section, const char* key, double fallback) {
        const double v = root.value(QLatin1String(section)).toObject().value(QLatin1String(key)).toDouble(0);
        return v > 0 ? v : fallback;
      };
      out.focusGapPx = num("FOCUS_RING", "gapPx", out.focusGapPx);
      out.focusWidthPx = num("FOCUS_RING", "widthPx", out.focusWidthPx);
      out.focusBlurPx = num("FOCUS_RING", "blurPx", out.focusBlurPx);
      out.focusGlowAlpha = num("FOCUS_RING", "glowAlpha", out.focusGlowAlpha);
      out.hoverGapPx = num("HOVER_RING", "gapPx", out.hoverGapPx);
      out.hoverWidthPx = num("HOVER_RING", "widthPx", out.hoverWidthPx);
      out.hoverAlpha = num("HOVER_RING", "alpha", out.hoverAlpha);
      out.pointGlowGapPx = num("SELECT_GLOW", "pointGapPx", out.pointGlowGapPx);
      out.pointGlowAlpha = num("SELECT_GLOW", "pointAlpha", out.pointGlowAlpha);
      out.lineGlowPadPx = num("SELECT_GLOW", "linePadPx", out.lineGlowPadPx);
      out.lineGlowAlpha = num("SELECT_GLOW", "lineAlpha", out.lineGlowAlpha);
      out.lineHoverPadPx = num("SELECT_GLOW", "lineHoverPadPx", out.lineHoverPadPx);
      out.lineHoverAlpha = num("SELECT_GLOW", "lineHoverAlpha", out.lineHoverAlpha);
      out.dividerLineWidthPx = num("COMPARE_DIVIDER", "lineWidthPx", out.dividerLineWidthPx);
      out.dividerKnobRadiusPx = num("COMPARE_DIVIDER", "knobRadiusPx", out.dividerKnobRadiusPx);
      out.dividerGrabSlackPx = num("COMPARE_DIVIDER", "grabSlackPx", out.dividerGrabSlackPx);
      return out;
    }();
    return t;
  }

}  // namespace stencil::gui::markMetrics
