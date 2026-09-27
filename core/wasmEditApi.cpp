// WebAssembly ABI: the line-list edits — the co-edit merge's keep mask, and the chain edits
// the browser runs in JS on its live lines (exported for the parity suite). Lines cross as the
// abi/linesCodec.hpp pair; a point list as a flat [x0,y0,…] array.

#include "linesCodec.hpp"
#include "lineChain.hpp"
#include "lineMerge.hpp"

#include <cstddef>
#include <cstdint>
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

  // keep[i] = 1 when local line i joins the merge; returns how many local lines were decoded,
  // of which the first keepCap are written.
  int stencil_mergeLinesKeep(const double* sNums, int sNumsLen, const std::uint8_t* sText,
                             int sTextLen, const double* lNums, int lNumsLen,
                             const std::uint8_t* lText, int lTextLen, std::uint8_t* keep,
                             int keepCap) {
    const Lines server = abi::decodeLines(sNums, sNumsLen, sText, sTextLen);
    const Lines local = abi::decodeLines(lNums, lNumsLen, lText, lTextLen);
    const std::vector<bool> k = mergeKeep(server, local);
    for (std::size_t i = 0; keep != nullptr && i < k.size() && static_cast<int>(i) < keepCap; ++i)
      keep[i] = k[i] ? 1 : 0;
    return static_cast<int>(k.size());
  }

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
