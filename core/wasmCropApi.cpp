// WebAssembly API surface: the crop-geometry exports.
//
// Split out of wasmApi.cpp on size; the same extern "C" contract applies (plain C
// over doubles and output pointers, ccall/cwrap from the browser). Wiring: core/WASM.md.

#include "cropGeometry.hpp"

using namespace stencil::core;

namespace {
  // Write a CropRect to out[0..3] = {x, y, width, height}.
  void writeRect(const CropRect& r, double* out) {
    out[0] = r.x;
    out[1] = r.y;
    out[2] = r.width;
    out[3] = r.height;
  }
}

extern "C" {

  // ── crop geometry (cropGeometry.js; shared with the Qt crop dialog) ──
  // Each CropRect result is written to out[0..3] = {x, y, width, height}.
  int stencil_isAlbumOrientation(double width, double height) {
    return isAlbumOrientation(width, height) ? 1 : 0;
  }

  double stencil_cropAspect(double pageWidth, double pageHeight, int album) {
    return cropAspect(pageWidth, pageHeight, album != 0);
  }

  void stencil_centeredCrop(double imageW, double imageH, double aspectWoverH,
                            double* out) {
    const CropRect r = centeredCrop(imageW, imageH, aspectWoverH);
    writeRect(r, out);
  }

  void stencil_resizeCropFromCorner(double x, double y, double w, double h,
                                    int corner, double cursorX, double cursorY,
                                    double aspectWoverH, double imageW,
                                    double imageH, double minSize, double* out) {
    const CropRect r = resizeCropFromCorner(CropRect{x, y, w, h}, corner, cursorX,
                                            cursorY, aspectWoverH, imageW, imageH,
                                            minSize);
    writeRect(r, out);
  }

  void stencil_moveCropClamped(double x, double y, double w, double h, double dx,
                               double dy, double imageW, double imageH,
                               double* out) {
    const CropRect r = moveCropClamped(CropRect{x, y, w, h}, dx, dy, imageW, imageH);
    writeRect(r, out);
  }

  void stencil_scaleCropCentered(double x, double y, double w, double h, double factor,
                                 double aspectWoverH, double imageW, double imageH,
                                 double* out) {
    const CropRect r = scaleCropCentered(CropRect{x, y, w, h}, factor, aspectWoverH,
                                         imageW, imageH);
    writeRect(r, out);
  }

  double stencil_cropResizeScale(double oldWidth, double newWidth) {
    return cropResizeScale(oldWidth, newWidth);
  }

  // Rotate a crop rect one quarter turn (clockwise != 0 = right) within an image
  // of imageW x imageH -> out[0..3] = {x, y, width, height}. Crop-local line
  // points ride along via the plain rotateLinePointsQuarter core call (the
  // browser runs that one in JS, like scaleLinePoints), so it needs no export.
  void stencil_rotateCropRectQuarter(double x, double y, double w, double h,
                                     double imageW, double imageH, int clockwise,
                                     double* out) {
    const CropRect r = rotateCropRectQuarter(CropRect{x, y, w, h}, imageW, imageH,
                                             clockwise != 0);
    writeRect(r, out);
  }

  // out[0] = orientationChanged (0/1), out[1] = scale.
  void stencil_cropChange(double oldX, double oldY, double oldW, double oldH,
                          double newX, double newY, double newW, double newH,
                          double* out) {
    const CropChange c = cropChange(CropRect{oldX, oldY, oldW, oldH},
                                    CropRect{newX, newY, newW, newH});
    out[0] = c.orientationChanged ? 1.0 : 0.0;
    out[1] = c.scale;
  }

}  // extern "C"
