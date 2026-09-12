#pragma once
#include "models.hpp"
#include <vector>

// Pure point/segment maths, no hit-test policy. Port of the geometry helpers in
// browser/js/utils/geometry.js and the transforms in core/lineTransforms.js.
namespace stencil::core {

  // Port of browser/js/utils/geometry.js `distToSegment`.
  double distToSegment(double px, double py, const Point& a, const Point& b);

  // In place about (cx, cy), `angle` in radians. Port of lineTransforms.js rotatePointsAbout.
  void rotatePoints(std::vector<Point>& points, double cx, double cy,
                    double angle);

  // In place about (cx, cy); horizontal reflects x, else y. Port of flipPointsAbout.
  void flipPoints(std::vector<Point>& points, bool horizontal, double cx,
                  double cy);

  // Port of lineTransforms.js bboxCenterOf; {0,0} for an empty list.
  Point boundingBoxCenter(const std::vector<Point>& points);

}
