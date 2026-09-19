#pragma once
#include "models.hpp"

// Crop-window geometry shared by the desktop and the wasm browser build. A crop is an
// axis-aligned rect in ORIGINAL-image pixel space; line and point coords are crop-local.
// The original is never modified, so the crop re-adjusts losslessly. Its aspect is
// fixed to the page (A3 = 42 / 29.7), so it resizes from a corner only.
namespace stencil::core {

  struct CropRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
  };

  bool isAlbumOrientation(double width, double height);

  // Width / height for a page of the given cm dimensions; album lays the long side
  // horizontally, whichever order the page's own dimensions come in.
  double cropAspect(double pageWidth, double pageHeight, bool album);

  // Largest centred rect of aspect `aspectWoverH` inside the image — the default crop.
  CropRect centeredCrop(double imageW, double imageH, double aspectWoverH);

  // Drag one corner with the aspect fixed and the opposite corner anchored.
  // corner: 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left.
  CropRect resizeCropFromCorner(const CropRect& cur, int corner, double cursorX,
                                double cursorY, double aspectWoverH,
                                double imageW, double imageH,
                                double minSize = 16.0);

  CropRect moveCropClamped(const CropRect& cur, double dx, double dy,
                           double imageW, double imageH);

  // Scale about the CENTRE with the aspect fixed: growth is capped by the nearer image
  // edge on each axis, neither side drops below `minSize`. The wheel / pinch resize.
  CropRect scaleCropCentered(const CropRect& cur, double factor, double aspectWoverH,
                             double imageW, double imageH, double minSize = 16.0);

  // The Album/Portrait press: a width/height swap about the same centre already lands on
  // `aspectWoverH` (one page's two aspects are exact reciprocals) — the user's own framing
  // carries over instead of resetting. No rect yet falls back to centeredCrop.
  CropRect swapCropOrientation(const CropRect& cur, double aspectWoverH,
                               double imageW, double imageH);

  // Uniform factor mapping crop-local points across a width change; 1.0 if oldWidth <= 0.
  double cropResizeScale(double oldWidth, double newWidth);

  // A flipped orientation invalidates the lines (the caller clears them after
  // confirming); otherwise the points are rescaled by `scale`.
  struct CropChange {
    bool orientationChanged = false;
    double scale = 1.0;
  };
  CropChange cropChange(const CropRect& oldRect, const CropRect& newRect);

  void scaleLinePoints(Lines& lines, double scale);

  // Quarter turns are non-destructive: a 0..3 clockwise count is stored beside the crop,
  // and these carry the crop window and its crop-local points across one turn.

  // `imageW x imageH` is the image the rect currently lives in; the turned one is
  // imageH x imageW. `clockwise` rotates the picture right.
  CropRect rotateCropRectQuarter(const CropRect& r, double imageW, double imageH,
                                 bool clockwise);

  // In place inside a crop box of boxW x boxH; after the turn the box is boxH x boxW.
  void rotateLinePointsQuarter(Lines& lines, double boxW, double boxH,
                               bool clockwise);

}
