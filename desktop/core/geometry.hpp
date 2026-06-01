#pragma once
#include "models.hpp"
#include <optional>
#include <vector>

// Pure geometry helpers. Port of the geometry section of browser/js/utils.js.
namespace stencil::core {

  // Distance from point (px, py) to the segment a -> b.
  double distToSegment(double px, double py, const Point& a, const Point& b);

  // Port of browser/js/core/drawingApp.js #closeCurrentShape gate (canvasClick):
  // a click closes the in-progress shape when it has >= 3 points and lands within
  // (markerSize + 8) image px of the first point. Threshold is in image space.
  bool shouldCloseShape(const std::vector<Point>& points, const Point& click,
                        double markerSize);

  // Port of browser/js/core/drawingApp.js:1527-1542 `findLineAt`. Reverse-
  // iterates the lines (topmost / last-drawn wins) and returns the index of the
  // first line within `threshold` px of (x, y): a point hit uses radius
  // `threshold + 4` (std::hypot), a segment hit uses `distToSegment` at
  // `threshold`. Returns -1 when nothing is hit.
  int findLineAt(const Lines& lines, double x, double y, double threshold = 8.0);

  // A located point: which line + which point within it. (lineIdx is an index
  // into the Lines passed to findNearestPoint.)
  struct PointHit {
    int lineIdx = -1;
    int ptIdx = -1;
  };

  // Port of browser/js/core/drawingApp.js #findNearestPointWithIdx (~1774).
  // Reverse-iterates the lines (topmost / last-drawn wins) and returns the first
  // point within `threshold` px of (x, y) using std::hypot. Returns nullopt when
  // nothing is within range. Default threshold (12) matches the browser.
  std::optional<PointHit> findNearestPoint(const Lines& lines, double x, double y,
                                           double threshold = 12.0);

  // A located segment: its line + the two consecutive point indices it spans.
  struct SegmentHit {
    int lineIdx = -1;
    int ptIdx1 = -1;
    int ptIdx2 = -1;
  };

  // Port of browser/js/core/drawingApp.js #findNearestSegmentWithIdx (~1793).
  // Returns the closest segment within `threshold` px of (x, y) across all lines
  // (reverse iteration, nearest distance wins), or nullopt. Default threshold 12.
  std::optional<SegmentHit> findNearestSegment(const Lines& lines, double x,
                                               double y, double threshold = 12.0);

  // Rotate `points` in place about pivot (cx, cy) by `angle` radians, using the
  // standard 2D rotation matrix. Port of #rotateSelectedLine (~1857).
  void rotatePoints(std::vector<Point>& points, double cx, double cy,
                    double angle);

  // Center of the axis-aligned bounding box of `points`. Port of the bbox-center
  // pivot in #rotateSelectedLine (~1844). Returns {0,0} for an empty list.
  Point boundingBoxCenter(const std::vector<Point>& points);

}
