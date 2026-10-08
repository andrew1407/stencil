#pragma once
#include <QImage>
#include <QRect>
#include <QSize>

// The desktop's seam onto core/raster/imageOps: the quarter-turn, the mirror and the crop the canvas shows,
// in Qt types. Clockwise turns, as core::rotateImageRows and the browser's model.js.
namespace stencil::model {

  // core::rotatedDims: the size after `quarters` clockwise turns.
  QSize turnedSize(const QSize& size, int quarters);

  // The rect of the UNturned picture that lands on `turned` once the picture is turned.
  QRect sourceRectOf(const QRect& turned, int quarters, const QSize& original);

  // The whole picture turned, once.
  QImage turn(const QImage& original, int quarters);

  // The whole picture mirrored left-right (core::mirrorImageRows).
  QImage mirror(const QImage& original);

  // `crop` (in turned space, clipped to it) of the turned picture: only that piece is copied
  // and turned, so a small crop of a large picture never turns the rest.
  QImage turnAndCrop(const QImage& original, int quarters, const QRect& crop);

}  // namespace stencil::model
