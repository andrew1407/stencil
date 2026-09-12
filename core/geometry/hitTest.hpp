#pragma once
#include "models.hpp"
#include "pointMath.hpp"
#include <optional>
#include <vector>

// Cursor hit tests over drawn lines. Port of browser/js/core/hitTest.js plus
// shouldCloseShape (core/lineTransforms.js) and holdDrawTarget (core/holdDraw.js).
namespace stencil::core {

  // lineTransforms.js shouldCloseShape: >= 3 points and the click within (pointSize + 8)
  // image px of the first point.
  bool shouldCloseShape(const std::vector<Point>& points, const Point& click,
                        double pointSize);

  // hitTest.js findLineAt: topmost (last-drawn) wins; a point hit uses radius
  // `threshold + 4`, a segment hit distToSegment at `threshold`. -1 when nothing is hit.
  int findLineAt(const Lines& lines, double x, double y, double threshold = 8.0);

  struct PointHit {
    int lineIdx = -1;
    int ptIdx = -1;
  };

  // hitTest.js findNearestPointWithIdx: topmost wins, strict `<` on std::hypot.
  std::optional<PointHit> findNearestPoint(const Lines& lines, double x, double y,
                                           double threshold = 12.0);

  // FIRST index within `threshold` of one point list (the in-progress-line scans).
  std::optional<int> nearestPointInLine(const std::vector<Point>& points,
                                        double x, double y,
                                        double threshold = 12.0);

  struct SegmentHit {
    int lineIdx = -1;
    int ptIdx1 = -1;
    int ptIdx2 = -1;
  };

  // hitTest.js findNearestSegmentWithIdx: nearest distance wins across all lines.
  std::optional<SegmentHit> findNearestSegment(const Lines& lines, double x,
                                               double y, double threshold = 12.0);

  // holdDraw.js holdDrawTarget: what an initial hold-to-draw press targets.
  enum class HoldTargetKind { NEW_LINE, CONTINUE_POINT, INSERT_SEGMENT };
  struct HoldTarget {
    HoldTargetKind kind = HoldTargetKind::NEW_LINE;
    int lineIdx = -1;  // line to continue / insert into (-1 for NewLine)
    int ptIdx = -1;    // ContinuePoint: the point; InsertSegment: first endpoint
    int ptIdx2 = -1;   // InsertSegment: second endpoint
  };

  // A point hit takes priority over a segment hit; empty space seeds a fresh line.
  HoldTarget holdDrawTarget(const Lines& lines, double x, double y,
                            double pointThreshold = 12.0,
                            double segThreshold = 12.0);

}
