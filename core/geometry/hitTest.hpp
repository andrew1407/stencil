#pragma once
#include "models.hpp"
#include "pointMath.hpp"
#include <optional>
#include <vector>

// Cursor hit tests over drawn lines. Port of browser/js/core/hitTest.js plus
// shouldCloseShape (core/lineTransforms.js) and holdDrawTarget (core/holdDraw.js).
namespace stencil::core {

  // Port of browser/js/core/lineTransforms.js shouldCloseShape: a click closes the
  // in-progress shape when it has >= 3 points and lands within (pointSize + 8)
  // image px of the first point. Threshold is in image space.
  bool shouldCloseShape(const std::vector<Point>& points, const Point& click,
                        double pointSize);

  // Port of browser/js/core/hitTest.js `findLineAt`. Reverse-iterates the lines
  // (topmost / last-drawn wins) and returns the index of the first line within
  // `threshold` px of (x, y): a point hit uses radius `threshold + 4`
  // (std::hypot), a segment hit uses `distToSegment` at `threshold`. Returns -1
  // when nothing is hit.
  int findLineAt(const Lines& lines, double x, double y, double threshold = 8.0);

  // A located point: which line + which point within it. (lineIdx is an index
  // into the Lines passed to findNearestPoint.)
  struct PointHit {
    int lineIdx = -1;
    int ptIdx = -1;
  };

  // Port of browser/js/core/hitTest.js `findNearestPointWithIdx`. Reverse-
  // iterates the lines (topmost / last-drawn wins) and returns the first point
  // within `threshold` px of (x, y) using std::hypot. Returns nullopt when
  // nothing is within range. Default threshold (12) matches the browser.
  std::optional<PointHit> findNearestPoint(const Lines& lines, double x, double y,
                                           double threshold = 12.0);

  // Scan a single line's `points` and return the FIRST index within `threshold`
  // px of (x, y) using std::hypot with a strict `<` comparison (matching the
  // in-progress-line cursor scans in canvasWidget; default 12 mirrors
  // findNearestPoint). Returns nullopt when nothing is within range. Unlike
  // findNearestPoint this operates on a single point list, not a Lines stack.
  std::optional<int> nearestPointInLine(const std::vector<Point>& points,
                                        double x, double y,
                                        double threshold = 12.0);

  // A located segment: its line + the two consecutive point indices it spans.
  struct SegmentHit {
    int lineIdx = -1;
    int ptIdx1 = -1;
    int ptIdx2 = -1;
  };

  // Port of browser/js/core/hitTest.js `findNearestSegmentWithIdx`. Returns the
  // closest segment within `threshold` px of (x, y) across all lines (reverse
  // iteration, nearest distance wins), or nullopt. Default threshold 12.
  std::optional<SegmentHit> findNearestSegment(const Lines& lines, double x,
                                               double y, double threshold = 12.0);

  // What an initial hold-to-draw press over (x, y) targets, given the committed
  // lines. Port of browser/js/core/holdDraw.js holdDrawTarget.
  enum class HoldTargetKind { NewLine, ContinuePoint, InsertSegment };
  struct HoldTarget {
    HoldTargetKind kind = HoldTargetKind::NewLine;
    int lineIdx = -1;  // line to continue / insert into (-1 for NewLine)
    int ptIdx = -1;    // ContinuePoint: the point; InsertSegment: first endpoint
    int ptIdx2 = -1;   // InsertSegment: second endpoint
  };

  // Decide the hold-to-draw seed: an existing point under the cursor → continue
  // that line from it; a line body (not a point) → insert a point there; empty
  // space → a fresh line. Point hit takes priority over a segment hit. Reuses
  // findNearestPoint / findNearestSegment so the semantics match the hit tests.
  HoldTarget holdDrawTarget(const Lines& lines, double x, double y,
                            double pointThreshold = 12.0,
                            double segThreshold = 12.0);

}
