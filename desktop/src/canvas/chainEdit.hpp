#pragma once
// Closing a shape, and the two ways back out of one: the desktop port of the chain
// helpers in browser/js/core/dragGestures.js — keep the two in step.
//
// A click-closed shape repeats its first point at the end (handleDrawingClick's close
// branch); a rect is locked without one. "The ring" here is always the points minus that
// duplicate. Header-only, Q_OBJECT-free, so it needs no MOC.
#include <cstddef>
#include <vector>

#include "models.hpp"

namespace stencil::gui::chain {

  // The line's points as a RING: the closing duplicate dropped, if it has one.
  inline std::vector<core::Point> ringPoints(const std::vector<core::Point>& points) {
    const std::size_t n = points.size();
    if (n >= 2 && points[0].x == points[n - 1].x && points[0].y == points[n - 1].y)
      return std::vector<core::Point>(points.begin(), points.end() - 1);
    return points;
  }

  // Open a ring at vertex k: re-rooted to start there and run back to a copy of it, which
  // becomes the free end — so the seam appears where the user pulled, not at point 0.
  inline std::vector<core::Point> openRingAt(const std::vector<core::Point>& points, int k) {
    const std::vector<core::Point> ring = ringPoints(points);
    const int n = static_cast<int>(ring.size());
    if (n == 0) return points;
    std::vector<core::Point> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) out.push_back(ring[static_cast<std::size_t>(((k + i) % n + n) % n)]);
    return out;
  }

  // A line that is no longer closed has no area to paint, so its fill goes with the shape:
  // "transparent" is the app's own "no fill", so the field comes up CLEARED if the line is
  // ever closed again rather than quietly holding a colour nothing is filling.
  inline void clearFill(core::Line& line) { line.fillColor = "transparent"; }

  // Unchain an area back into an open polyline: the closing duplicate goes, it stops being
  // fillable, and its fill is cleared. Returns whether anything changed.
  inline bool unchainLine(core::Line& line) {
    if (!line.locked) return false;
    line.points = ringPoints(line.points);
    line.locked = false;
    clearFill(line);
    return true;
  }

  // What a pull-out gesture grabbed: a vertex, or a point on a segment body.
  struct PullTarget {
    bool onPoint = false;   // true: `index` is the vertex; false: it is the segment's 2nd end
    int index = -1;
  };

  // Where Alt+Ctrl+drag puts its new point: duplicated in place on a vertex, at the cursor
  // on a segment body. A locked line is opened there first, so the same gesture that adds a
  // point breaks a shape. Mutates `line`; returns the index to drag, or -1.
  inline int pullOutPoint(core::Line& line, const PullTarget& target, double x, double y) {
    const int n = static_cast<int>(line.points.size());
    if (target.index < 0) return -1;
    if (line.locked) {
      line.points = openRingAt(line.points, target.index);
      line.locked = false;
      clearFill(line);
      const int last = static_cast<int>(line.points.size()) - 1;
      // The seam is the last point — for a segment grab, slide it onto the cursor so the
      // break follows the pointer instead of snapping to the vertex.
      if (!target.onPoint && last >= 0) { line.points[static_cast<std::size_t>(last)] = {x, y}; }
      return last;
    }
    if (target.onPoint) {
      if (target.index >= n) return -1;
      const core::Point p = line.points[static_cast<std::size_t>(target.index)];
      line.points.insert(line.points.begin() + target.index + 1, p);
      return target.index + 1;
    }
    if (target.index > n) return -1;
    line.points.insert(line.points.begin() + target.index, core::Point{x, y});
    return target.index;
  }

}  // namespace stencil::gui::chain
