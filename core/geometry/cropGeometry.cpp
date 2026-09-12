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
    // Width-bound first; if the height overflows, height-bound instead.
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

    const bool movingLeft = (corner == 0 || corner == 3);
    const bool movingTop = (corner == 0 || corner == 1);
    const double anchorX = movingLeft ? cur.x + cur.width : cur.x;
    const double anchorY = movingTop ? cur.y + cur.height : cur.y;

    // Anchor-to-cursor distance per axis, non-negative.
    const double dx = std::max(0.0, movingLeft ? anchorX - cursorX
                                                : cursorX - anchorX);
    const double dy = std::max(0.0, movingTop ? anchorY - cursorY
                                               : cursorY - anchorY);

    // The axis demanding the larger rect wins, so the cursor stays on or inside the edge.
    double w = std::max(dx, dy * aspectWoverH);

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

  CropRect scaleCropCentered(const CropRect& cur, double factor, double aspectWoverH,
                             double imageW, double imageH, double minSize) {
    if (factor <= 0.0 || cur.width <= 0.0 || cur.height <= 0.0 || aspectWoverH <= 0.0) return cur;
    const double cx = cur.x + cur.width * 0.5;
    const double cy = cur.y + cur.height * 0.5;
    double w = cur.width * factor;
    double h = w / aspectWoverH;
    if (w < minSize) { w = minSize; h = w / aspectWoverH; }
    if (h < minSize) { h = minSize; w = h * aspectWoverH; }
    // With the centre fixed, each axis is limited by its NEARER edge.
    const double maxHalfW = std::min(cx, imageW - cx);
    const double maxHalfH = std::min(cy, imageH - cy);
    const double wMax = std::min(2.0 * maxHalfW, 2.0 * maxHalfH * aspectWoverH);
    if (wMax > 0.0 && w > wMax) { w = wMax; h = w / aspectWoverH; }
    double x = cx - w * 0.5;
    double y = cy - h * 0.5;
    // Floating-point drift guard.
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;
    if (x + w > imageW) x = imageW - w;
    if (y + h > imageH) y = imageH - h;
    return CropRect{x, y, w, h};
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

  CropRect rotateCropRectQuarter(const CropRect& r, double imageW, double imageH,
                                 bool clockwise) {
    CropRect out;
    out.width = r.height;
    out.height = r.width;
    if (clockwise) {
      // The picture's top-left lands top-right: x' counts down from imageH.
      out.x = imageH - (r.y + r.height);
      out.y = r.x;
    } else {
      out.x = r.y;
      out.y = imageW - (r.x + r.width);
    }
    return out;
  }

  void rotateLinePointsQuarter(Lines& lines, double boxW, double boxH,
                               bool clockwise) {
    for (auto& line : lines)
      for (auto& p : line.points) {
        const double px = p.x;
        const double py = p.y;
        if (clockwise) {
          p.x = boxH - py;
          p.y = px;
        } else {
          p.x = py;
          p.y = boxW - px;
        }
      }
  }

}
