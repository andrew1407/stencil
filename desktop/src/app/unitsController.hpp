#pragma once
#include <QString>
#include <cmath>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QWidget;

namespace stencil::gui {

  // The page-format and display-unit group; the arithmetic shared with browser/js/core/units.js.
  // Core-free by design.
  class UnitsController {
   public:
    QComboBox* pageSize = nullptr;
    QWidget* customGroup = nullptr;   // shown only for the "custom" format
    QDoubleSpinBox* customW = nullptr;
    QDoubleSpinBox* customH = nullptr;
    QComboBox* unitCombo = nullptr;   // toolbar cm/in switch (mirrors the menu)
    QAction* unitCm = nullptr;
    QAction* unitIn = nullptr;

    static constexpr double kInchPerCm = 1.0 / 2.54;

    static bool isInches(const QString& code) { return code == QLatin1String("in"); }
    static QString canonicalUnit(const QString& code) {
      return isInches(code) ? QStringLiteral("in") : QStringLiteral("cm");
    }
    // Model values are always centimetres.
    static double factor(const QString& code) { return isInches(code) ? kInchPerCm : 1.0; }
    static const char* label(const QString& code) { return isInches(code) ? "in" : "cm"; }
    // Inches need the extra digit to carry as much as one centimetre decimal.
    static int decimalsFor(const QString& code) { return isInches(code) ? 2 : 1; }

    // The scale behind core::pixelToPageRaw, not the formula path, so lengths are unit- and
    // formula-independent (browser units.js layoutLineLengthCm). 0 when nothing to measure.
    struct Scale {
      double x = 0.0, y = 0.0;
      bool measurable() const { return x > 0.0 && y > 0.0; }
    };
    static Scale pxToCm(double pageW, double pageH, int imgW, int imgH) {
      if (imgW <= 0 || imgH <= 0 || pageW <= 0.0 || pageH <= 0.0) return {};
      return {pageW / imgW, pageH / imgH};
    }
    static double segmentCm(double dxPx, double dyPx, const Scale& s) {
      return std::hypot(dxPx * s.x, dyPx * s.y);
    }
  };

}  // namespace stencil::gui
