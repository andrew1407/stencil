#pragma once
#include "models.hpp"

#include <vector>

// Closing a shape and the two ways back out of one. Port of ringPoints / openRingAt /
// unchainLine / pullOutPoint in browser/js/core/touch/dragGestures.js. A click-closed shape
// repeats its first point at the end, a rect is locked without one; "the ring" drops that copy.
namespace stencil::core::chain {

  std::vector<Point> ringPoints(const std::vector<Point>& points);

  // Re-rooted to start at k and run back to a copy of it, so the seam is where the user pulled.
  std::vector<Point> openRingAt(const std::vector<Point>& points, int k);

  // "transparent" is the app's own "no fill", so the field comes up CLEARED if closed again.
  void clearFill(Line& line);

  // Returns whether anything changed.
  bool unchainLine(Line& line);

  struct PullTarget {
    bool onPoint = false;  // true: `index` is the vertex; false: it is the segment's 2nd end
    int index = -1;
  };

  // Duplicated in place on a vertex, at the cursor on a segment; a locked line is opened there
  // first. Mutates `line`; returns the index to drag, or -1.
  int pullOutPoint(Line& line, const PullTarget& target, double x, double y);

}
