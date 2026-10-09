#pragma once
#include "hitTest.hpp"

// The desktop's seam onto core/geometry/hitTest for the pointer: only what the scene draws is hit —
// a hidden point is never a point target, hidden lines never a segment target, with both hidden
// nothing is, and a line hidden on its own is no target at all. Radii are image px. Browser twin:
// core/pointer/markHits.js.
namespace stencil::model {

  // Which marks the scene paints (CanvasScene::shownMarks).
  struct ShownMarks {
    bool points = true;
    bool lines = true;
  };

  inline std::optional<core::PointHit> pointAt(const core::Lines& lines, ShownMarks shown, double x,
                                               double y, double radius) {
    if (!shown.points) return std::nullopt;
    return core::findNearestPoint(lines, x, y, radius);
  }

  // The hover's point, in hitTest.js's order with no copy: committed lines bottom up, then the
  // stroke in progress; the first line holding a point within `radius`, and its first such point.
  inline std::optional<core::Point> firstPointWithin(const core::Lines& committed, const core::Line& current,
                                                     ShownMarks shown, double x, double y, double radius) {
    if (!shown.points) return std::nullopt;
    for (const core::Line& l : committed)
      if (const auto i = l.hidden ? std::nullopt : core::nearestPointInLine(l.points, x, y, radius))
        return l.points[*i];
    if (const auto i = current.hidden ? std::nullopt : core::nearestPointInLine(current.points, x, y, radius))
      return current.points[*i];
    return std::nullopt;
  }

  // A vertex of one point list (the in-progress stroke).
  inline std::optional<int> pointIn(const std::vector<core::Point>& points, ShownMarks shown, double x,
                                    double y, double radius) {
    if (!shown.points) return std::nullopt;
    return core::nearestPointInLine(points, x, y, radius);
  }

  inline std::optional<core::SegmentHit> segmentAt(const core::Lines& lines, ShownMarks shown, double x,
                                                   double y, double radius) {
    if (!shown.lines) return std::nullopt;
    return core::findNearestSegment(lines, x, y, radius);
  }

  // core::findLineAt over what shows: the stroke alone while points are hidden, the points alone
  // while lines are, each within `radius`; -1 when neither shows.
  inline int lineAt(const core::Lines& lines, ShownMarks shown, double x, double y, double radius) {
    if (shown.points && shown.lines) return core::findLineAt(lines, x, y, radius);
    if (const auto seg = segmentAt(lines, shown, x, y, radius)) return seg->lineIdx;
    if (const auto pt = pointAt(lines, shown, x, y, radius)) return pt->lineIdx;
    return -1;
  }

  // A hidden kind's radius is 0, which reaches nothing: a hold never continues a hidden point or
  // inserts into a hidden line, it starts a fresh one.
  inline core::HoldTarget holdTargetAt(const core::Lines& lines, ShownMarks shown, double x, double y,
                                       double radius) {
    return core::holdDrawTarget(lines, x, y, shown.points ? radius : 0.0,
                                shown.lines ? radius : 0.0);
  }

}  // namespace stencil::model
