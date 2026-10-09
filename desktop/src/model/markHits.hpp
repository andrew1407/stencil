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

  // `lines` with each hidden one swapped for a mark-less stand-in, indices kept (hittableLines); the
  // same lines, uncopied, when none hides.
  inline const core::Lines& hittable(const core::Lines& lines, core::Lines& scratch) {
    bool any = false;
    for (const core::Line& l : lines) any = any || l.hidden;
    if (!any) return lines;
    scratch.clear();
    scratch.reserve(lines.size());
    for (const core::Line& l : lines) scratch.push_back(l.hidden ? core::Line{} : l);
    return scratch;
  }

  inline std::optional<core::PointHit> pointAt(const core::Lines& lines, ShownMarks shown, double x,
                                               double y, double radius) {
    if (!shown.points) return std::nullopt;
    core::Lines scratch;
    return core::findNearestPoint(hittable(lines, scratch), x, y, radius);
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
    core::Lines scratch;
    return core::findNearestSegment(hittable(lines, scratch), x, y, radius);
  }

  // core::findLineAt over what shows: the stroke alone while points are hidden, the points alone
  // while lines are, each within `radius`; -1 when neither shows.
  inline int lineAt(const core::Lines& lines, ShownMarks shown, double x, double y, double radius) {
    core::Lines scratch;
    if (shown.points && shown.lines) return core::findLineAt(hittable(lines, scratch), x, y, radius);
    if (const auto seg = segmentAt(lines, shown, x, y, radius)) return seg->lineIdx;
    if (const auto pt = pointAt(lines, shown, x, y, radius)) return pt->lineIdx;
    return -1;
  }

  // A hidden kind's radius is 0, which reaches nothing: a hold never continues a hidden point or
  // inserts into a hidden line, it starts a fresh one.
  inline core::HoldTarget holdTargetAt(const core::Lines& lines, ShownMarks shown, double x, double y,
                                       double radius) {
    core::Lines scratch;
    return core::holdDrawTarget(hittable(lines, scratch), x, y, shown.points ? radius : 0.0,
                                shown.lines ? radius : 0.0);
  }

}  // namespace stencil::model
