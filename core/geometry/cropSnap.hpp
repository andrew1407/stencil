#pragma once
#include "cropGeometry.hpp"

// Committing a crop window: integer pixels inside the turned original, and one quarter-turn of
// the whole edit. Port of ImageModel.roundRect / rotateImage (browser/js/core/image/model.js),
// JS twins in browser/js/core/parse/cropGeometry.js.
namespace stencil::core {

  // Each side rounded as Math.round does and kept in [1, the image's side]; then the origin,
  // moved inside. `imageW x imageH` is the (turned) original the crop lives in.
  CropRect snapCropRect(const CropRect& r, double imageW, double imageH);

  struct EditTurn {
    CropRect crop;
    int quarters = 0;
  };

  // One quarter-turn of an unturned `originalW x originalH` picture shown at `quarters`: the
  // window follows into the turned space and is snapped there; the count wraps to 0..3.
  EditTurn rotateEditQuarter(const CropRect& crop, int quarters, double originalW,
                             double originalH, bool clockwise);

  // The same, turning the crop-local lines inside the old window first.
  EditTurn rotateEditQuarter(Lines& lines, const CropRect& crop, int quarters,
                             double originalW, double originalH, bool clockwise);

  // A left-right flip of the shown picture, which is turn(quarters) of mirror^m(original): the
  // caller toggles m, the count becomes -quarters, and the window reflects across the turned width.
  EditTurn mirrorEdit(const CropRect& crop, int quarters, double originalW, double originalH);

  // The same, mirroring the crop-local lines inside the window first.
  EditTurn mirrorEdit(Lines& lines, const CropRect& crop, int quarters, double originalW,
                      double originalH);

}
