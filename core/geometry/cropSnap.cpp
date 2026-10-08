#include "cropSnap.hpp"

#include <cmath>

namespace stencil::core {

  namespace {
    // Math.round: halves go up, so -2.5 -> -2, and 0.49999999999999994 stays 0.
    double jsRound(double v) {
      if (!std::isfinite(v)) return v;
      const double f = std::floor(v);
      return v - f >= 0.5 ? f + 1.0 : f;
    }

    // Math.min / Math.max: NaN wins, and a zero tie keeps the -0 / +0 JS picks.
    double jsMin(double a, double b) {
      if (std::isnan(a) || std::isnan(b)) return NAN;
      if (a == b) return std::signbit(a) ? a : b;
      return a < b ? a : b;
    }

    double jsMax(double a, double b) {
      if (std::isnan(a) || std::isnan(b)) return NAN;
      if (a == b) return std::signbit(a) ? b : a;
      return a > b ? a : b;
    }
  }  // namespace

  CropRect snapCropRect(const CropRect& r, double imageW, double imageH) {
    CropRect out;
    out.width = jsMax(1.0, jsMin(jsRound(r.width), imageW));
    out.height = jsMax(1.0, jsMin(jsRound(r.height), imageH));
    out.x = jsMax(0.0, jsMin(jsRound(r.x), imageW - out.width));
    out.y = jsMax(0.0, jsMin(jsRound(r.y), imageH - out.height));
    return out;
  }

  EditTurn rotateEditQuarter(const CropRect& crop, int quarters, double originalW,
                             double originalH, bool clockwise) {
    const bool odd = quarters % 2 != 0;
    const double iw = odd ? originalH : originalW;
    const double ih = odd ? originalW : originalH;
    EditTurn out;
    out.quarters = (((quarters + (clockwise ? 1 : -1)) % 4) + 4) % 4;
    out.crop = snapCropRect(rotateCropRectQuarter(crop, iw, ih, clockwise), ih, iw);
    return out;
  }

  EditTurn rotateEditQuarter(Lines& lines, const CropRect& crop, int quarters,
                             double originalW, double originalH, bool clockwise) {
    rotateLinePointsQuarter(lines, crop.width, crop.height, clockwise);
    return rotateEditQuarter(crop, quarters, originalW, originalH, clockwise);
  }

  EditTurn mirrorEdit(const CropRect& crop, int quarters, double originalW, double originalH) {
    const bool odd = quarters % 2 != 0;
    const double iw = odd ? originalH : originalW;
    const double ih = odd ? originalW : originalH;
    EditTurn out;
    out.quarters = ((-quarters % 4) + 4) % 4;
    out.crop = snapCropRect(CropRect{iw - crop.x - crop.width, crop.y, crop.width, crop.height},
                            iw, ih);
    return out;
  }

  EditTurn mirrorEdit(Lines& lines, const CropRect& crop, int quarters, double originalW,
                      double originalH) {
    mirrorLinePoints(lines, crop.width);
    return mirrorEdit(crop, quarters, originalW, originalH);
  }

}
