#include "lineChain.hpp"

#include <cstddef>

namespace stencil::core::chain {

  std::vector<Point> ringPoints(const std::vector<Point>& points) {
    const std::size_t n = points.size();
    if (n >= 2 && points[0].x == points[n - 1].x && points[0].y == points[n - 1].y)
      return std::vector<Point>(points.begin(), points.end() - 1);
    return points;
  }

  std::vector<Point> openRingAt(const std::vector<Point>& points, int k) {
    const std::vector<Point> ring = ringPoints(points);
    const long long n = static_cast<long long>(ring.size());
    if (n == 0) return points;
    std::vector<Point> out;
    out.reserve(ring.size() + 1);
    for (long long i = 0; i <= n; ++i)
      out.push_back(ring[static_cast<std::size_t>((((k + i) % n) + n) % n)]);
    return out;
  }

  void clearFill(Line& line) { line.fillColor = "transparent"; }

  bool unchainLine(Line& line) {
    if (!line.locked) return false;
    line.points = ringPoints(line.points);
    line.locked = false;
    clearFill(line);
    return true;
  }

  int pullOutPoint(Line& line, const PullTarget& target, double x, double y) {
    const int n = static_cast<int>(line.points.size());
    if (line.locked) {
      line.points = openRingAt(line.points, target.index);
      line.locked = false;
      clearFill(line);
      const int last = static_cast<int>(line.points.size()) - 1;
      // A segment grab slides the seam onto the cursor, so the break follows the pointer.
      if (!target.onPoint && last >= 0) line.points[static_cast<std::size_t>(last)] = {x, y};
      return last;
    }
    if (target.onPoint) {
      if (target.index < 0 || target.index >= n) return -1;
      const Point p = line.points[static_cast<std::size_t>(target.index)];
      line.points.insert(line.points.begin() + target.index + 1, p);
      return target.index + 1;
    }
    if (target.index < 0 || target.index > n) return -1;
    line.points.insert(line.points.begin() + target.index, Point{x, y});
    return target.index;
  }

}
