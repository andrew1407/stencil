#include "cropGeometry.hpp"
#include <algorithm>

namespace stencil::core {

  bool isAlbumOrientation(double width, double height) { return width > height; }

  double cropAspect(double pageWidth, double pageHeight, bool album) {
    const double lo = std::min(pageWidth, pageHeight);
    const double hi = std::max(pageWidth, pageHeight);
    if (lo <= 0.0 || hi <= 0.0) return 1.0;
    return album ? (hi / lo) : (lo / hi);
  }

  CropRect centeredCrop(double imageW, double imageH, double aspectWoverH) {
    CropRect r;
    if (imageW <= 0.0 || imageH <= 0.0 || aspectWoverH <= 0.0) return r;
    // Start as wide as the image, then shrink height to match the aspect; if that
    // overflows the image height, pin to the height and derive the width instead.
    double w = imageW;
    double h = w / aspectWoverH;
    if (h > imageH) {
      h = imageH;
      w = h * aspectWoverH;
    }
    r.width = w;
    r.height = h;
    r.x = (imageW - w) / 2.0;
    r.y = (imageH - h) / 2.0;
    return r;
  }

  CropRect resizeCropFromCorner(const CropRect& cur, int corner, double cursorX,
                                double cursorY, double aspectWoverH,
                                double imageW, double imageH, double minSize) {
    if (aspectWoverH <= 0.0 || imageW <= 0.0 || imageH <= 0.0) return cur;

    // The moving corner's side; the diagonally-opposite corner stays anchored.
    const bool movingLeft = (corner == 0 || corner == 3);
    const bool movingTop = (corner == 0 || corner == 1);
    const double anchorX = movingLeft ? cur.x + cur.width : cur.x;
    const double anchorY = movingTop ? cur.y + cur.height : cur.y;

    // Distance from the anchor to the cursor along each axis (the rectangle grows
    // toward the cursor), clamped to be non-negative.
    const double dx = std::max(0.0, movingLeft ? anchorX - cursorX
                                                : cursorX - anchorX);
    const double dy = std::max(0.0, movingTop ? anchorY - cursorY
                                               : cursorY - anchorY);

    // Take whichever axis demands the larger rectangle so the cursor stays on or
    // inside the edge, then fold the aspect ratio back in.
    double w = std::max(dx, dy * aspectWoverH);

    // Room available from the anchor toward the moving direction, on both axes.
    const double availW = movingLeft ? anchorX : (imageW - anchorX);
    const double availH = movingTop ? anchorY : (imageH - anchorY);
    const double maxW = std::min(availW, availH * aspectWoverH);

    if (maxW < minSize) {
      w = maxW;  // image is too small for minSize in this direction — use it all
    } else {
      w = std::min(std::max(w, minSize), maxW);
    }
    const double h = w / aspectWoverH;

    CropRect r;
    r.width = w;
    r.height = h;
    r.x = movingLeft ? anchorX - w : anchorX;
    r.y = movingTop ? anchorY - h : anchorY;
    return r;
  }

  CropRect moveCropClamped(const CropRect& cur, double dx, double dy,
                           double imageW, double imageH) {
    CropRect r = cur;
    const double maxX = std::max(0.0, imageW - cur.width);
    const double maxY = std::max(0.0, imageH - cur.height);
    r.x = std::min(std::max(cur.x + dx, 0.0), maxX);
    r.y = std::min(std::max(cur.y + dy, 0.0), maxY);
    return r;
  }

  double cropResizeScale(double oldWidth, double newWidth) {
    return oldWidth > 0.0 ? newWidth / oldWidth : 1.0;
  }

  CropChange cropChange(const CropRect& oldRect, const CropRect& newRect) {
    CropChange c;
    c.orientationChanged =
        isAlbumOrientation(oldRect.width, oldRect.height) !=
        isAlbumOrientation(newRect.width, newRect.height);
    c.scale = c.orientationChanged
                  ? 1.0
                  : cropResizeScale(oldRect.width, newRect.width);
    return c;
  }

  void scaleLinePoints(Lines& lines, double scale) {
    for (auto& line : lines)
      for (auto& p : line.points) {
        p.x *= scale;
        p.y *= scale;
      }
  }

}
