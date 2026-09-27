// WebAssembly ABI: the crop-geometry exports (same contract as wasmApi.cpp). Each
// CropRect result is written to out[0..3] = {x, y, width, height}.

#include "cropGeometry.hpp"

using namespace stencil::core;

namespace {
  void writeRect(const CropRect& r, double* out) {
    out[0] = r.x;
    out[1] = r.y;
    out[2] = r.width;
    out[3] = r.height;
  }
}

extern "C" {

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

  void stencil_swapCropOrientation(double x, double y, double w, double h,
                                   double aspectWoverH, double imageW,
                                   double imageH, double* out) {
    const CropRect r = swapCropOrientation(CropRect{x, y, w, h}, aspectWoverH,
                                           imageW, imageH);
    writeRect(r, out);
  }

  double stencil_cropResizeScale(double oldWidth, double newWidth) {
    return cropResizeScale(oldWidth, newWidth);
  }

  // rotateLinePointsQuarter has no export: the browser runs it in JS, like scaleLinePoints.
  void stencil_rotateCropRectQuarter(double x, double y, double w, double h,
                                     double imageW, double imageH, int clockwise,
                                     double* out) {
    const CropRect r = rotateCropRectQuarter(CropRect{x, y, w, h}, imageW, imageH,
                                             clockwise != 0);
    writeRect(r, out);
  }

}  // extern "C"
