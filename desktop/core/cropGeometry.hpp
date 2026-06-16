#pragma once
#include "models.hpp"

// Crop-window geometry, shared by the Qt desktop app and the WebAssembly browser
// build (the math is the same in both front-ends, so it lives here once). A crop
// is an axis-aligned rectangle in ORIGINAL-image pixel space; the main canvas
// shows exactly that sub-rectangle, and line/marker points live in crop-local
// pixels (0..width, 0..height). The original image is never modified — only the
// rectangle is stored — so the crop can be re-adjusted (moved, resized, or
// flipped between album/portrait) losslessly.
//
// The crop's aspect ratio is fixed to the page (e.g. A3 = 42 / 29.7 ≈ √2), so it
// can only be resized from a corner; the page relation is preserved across edits.
namespace stencil::core {

  // An axis-aligned crop window in original-image pixel space.
  struct CropRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
  };

  // Landscape ("album") when wider than tall. Mirrors the page-orientation test
  // used by pageDimensions (canvasWidth > canvasHeight).
  bool isAlbumOrientation(double width, double height);

  // Target crop aspect (width / height) for a page of the given cm dimensions in
  // the chosen orientation. Album lays the page's long side horizontally; the
  // page's own width/height order is ignored — only its proportions matter.
  double cropAspect(double pageWidth, double pageHeight, bool album);

  // Largest rectangle of aspect `aspectWoverH` (width / height) that fits inside
  // imageW x imageH, centered — cutting the surplus off the two opposite sides.
  // This is the default crop applied when an image is first loaded.
  CropRect centeredCrop(double imageW, double imageH, double aspectWoverH);

  // Resize a crop by dragging one corner: keeps `aspectWoverH` fixed, anchors the
  // diagonally-opposite corner, and clamps to the image bounds and `minSize`.
  // corner: 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left.
  CropRect resizeCropFromCorner(const CropRect& cur, int corner, double cursorX,
                                double cursorY, double aspectWoverH,
                                double imageW, double imageH,
                                double minSize = 16.0);

  // Translate a crop by (dx, dy), clamped so it stays fully inside the image.
  CropRect moveCropClamped(const CropRect& cur, double dx, double dy,
                           double imageW, double imageH);

  // Uniform scale that maps crop-local points from an old crop width to a new one
  // (aspect is preserved, so the same factor applies to x and y). 1.0 if oldWidth
  // is non-positive.
  double cropResizeScale(double oldWidth, double newWidth);

  // What re-cropping implies for existing lines. A flipped orientation
  // invalidates them (the caller clears the lines after confirming with the
  // user); otherwise the points are rescaled by `scale` to keep their position
  // relative to the page.
  struct CropChange {
    bool orientationChanged = false;
    double scale = 1.0;
  };
  CropChange cropChange(const CropRect& oldRect, const CropRect& newRect);

  // Multiply every point of every line by `scale` in place — the crop-local
  // rescale applied when the crop is resized within the same orientation.
  void scaleLinePoints(Lines& lines, double scale);

  // ── 90° image rotation (shared by the desktop app and the wasm browser build) ──
  // Rotation is non-destructive: the original image is kept and a quarter-turn
  // count (0..3, clockwise) is stored alongside the crop rectangle. These helpers
  // transport the crop window and the crop-local line points across a single
  // quarter turn so the framing and the drawing follow the rotated picture.

  // Rotate a crop rectangle one quarter turn within an image of imageW x imageH
  // (the dimensions of the image the rect currently lives in). The turned image's
  // dimensions are imageH x imageW. `clockwise` rotates the picture right.
  CropRect rotateCropRectQuarter(const CropRect& r, double imageW, double imageH,
                                 bool clockwise);

  // Rotate every crop-local point of every line one quarter turn inside a crop
  // box of boxW x boxH (in crop-local pixels), in place. After the turn the box
  // is boxH x boxW. `clockwise` matches rotateCropRectQuarter.
  void rotateLinePointsQuarter(Lines& lines, double boxW, double boxH,
                               bool clockwise);

}
