#pragma once
#include "models.hpp"
#include <vector>

// Pure point/segment maths, no hit-test policy. Port of the geometry helpers in
// browser/js/utils/geometry.js and the transforms in core/lineTransforms.js.
namespace stencil::core {

  // Port of browser/js/utils/geometry.js `distToSegment`.
  double distToSegment(double px, double py, const Point& a, const Point& b);

  // Rotate `points` in place about pivot (cx, cy) by `angle` radians, using the
  // standard 2D rotation matrix. Port of lineTransforms.js rotatePointsAbout.
  void rotatePoints(std::vector<Point>& points, double cx, double cy,
                    double angle);

  // Mirror `points` in place about pivot (cx, cy): horizontal reflects each x
  // (x' = 2*cx - x), else reflects each y (y' = 2*cy - y). Port of
  // lineTransforms.js flipPointsAbout.
  void flipPoints(std::vector<Point>& points, bool horizontal, double cx,
                  double cy);

  // Center of the axis-aligned bounding box of `points`. Port of
  // lineTransforms.js bboxCenterOf. Returns {0,0} for an empty list.
  Point boundingBoxCenter(const std::vector<Point>& points);

}
