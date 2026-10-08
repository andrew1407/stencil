// WebAssembly ABI: the chain edits the browser runs in JS on its live lines (exported for the
// parity suite); a point list crosses as a flat [x0,y0,…] array. The co-edit merge's keep mask
// is in abi/shared.inc.

#include "lineChain.hpp"

#include <cstddef>
#include <vector>

using namespace stencil::core;

namespace {
  std::vector<Point> pointsOf(const double* pts, int n) {
    std::vector<Point> out;
    if (pts == nullptr || n <= 0) return out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) out.push_back(Point{pts[2 * i], pts[2 * i + 1]});
    return out;
  }

  void writePoints(const std::vector<Point>& pts, double* out) {
    if (out == nullptr) return;
    for (std::size_t i = 0; i < pts.size(); ++i) {
      out[2 * i] = pts[i].x;
      out[2 * i + 1] = pts[i].y;
    }
  }
}  // namespace

extern "C" {

  // The ring without its closing copy into out (n points of room); the new point count, or -1
  // when the line is not an area and nothing changed.
  int stencil_chainUnchain(const double* pts, int n, int locked, double* out) {
    Line line;
    line.points = pointsOf(pts, n);
    line.locked = locked != 0;
    if (!chain::unchainLine(line)) return -1;
    writePoints(line.points, out);
    return static_cast<int>(line.points.size());
  }

  // The index to drag, or -1; out gets the new points (n + 1 of room), outCount[0] their count.
  int stencil_chainPullOut(const double* pts, int n, int locked, int onPoint, int index,
                           double x, double y, double* out, int* outCount) {
    Line line;
    line.points = pointsOf(pts, n);
    line.locked = locked != 0;
    const int at = chain::pullOutPoint(line, chain::PullTarget{onPoint != 0, index}, x, y);
    writePoints(line.points, out);
    if (outCount != nullptr) outCount[0] = static_cast<int>(line.points.size());
    return at;
  }

}  // extern "C"
