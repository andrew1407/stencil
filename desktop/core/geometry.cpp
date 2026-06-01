#include "geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace stencil::core {

  // Port of browser/js/utils.js `distToSegment`. Clamps the projection
  // parameter t to [0, 1] so the result is distance to the segment, not the
  // infinite line. A zero-length segment degenerates to point distance.
  double distToSegment(double px, double py, const Point& a, const Point& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq == 0.0) return std::hypot(px - a.x, py - a.y);
    double t = ((px - a.x) * dx + (py - a.y) * dy) / lenSq;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(px - (a.x + t * dx), py - (a.y + t * dy));
  }

  // Port of browser/js/core/drawingApp.js #closeCurrentShape gate.
  bool shouldCloseShape(const std::vector<Point>& points, const Point& click,
                        double markerSize) {
    if (points.size() < 3) return false;
    const Point& first = points.front();
    const double d = std::hypot(click.x - first.x, click.y - first.y);
    return d <= markerSize + 8.0;
  }

  // Port of browser/js/core/drawingApp.js:1527-1542 `findLineAt`. Reverse
  // iteration so the topmost (last-drawn) line wins on overlap. Per line we test
  // every point at radius `threshold + 4`, then every segment at `threshold`.
  int findLineAt(const Lines& lines, double x, double y, double threshold) {
    for (std::size_t i = lines.size(); i-- > 0;) {
      const std::vector<Point>& pts = lines[i].points;

      // Check points (drawingApp.js:1534-1535).
      for (const Point& p : pts)
        if (std::hypot(p.x - x, p.y - y) <= threshold + 4.0)
          return static_cast<int>(i);

      // Check segments (drawingApp.js:1538-1539).
      for (std::size_t j = 0; j + 1 < pts.size(); ++j)
        if (distToSegment(x, y, pts[j], pts[j + 1]) <= threshold)
          return static_cast<int>(i);
    }
    return -1;
  }

  // Port of browser/js/core/drawingApp.js #findNearestPointWithIdx (~1774).
  std::optional<PointHit> findNearestPoint(const Lines& lines, double x, double y,
                                           double threshold) {
    for (std::size_t li = lines.size(); li-- > 0;) {
      const std::vector<Point>& pts = lines[li].points;
      for (std::size_t pi = 0; pi < pts.size(); ++pi) {
        if (std::hypot(pts[pi].x - x, pts[pi].y - y) < threshold)
          return PointHit{static_cast<int>(li), static_cast<int>(pi)};
      }
    }
    return std::nullopt;
  }

  // Port of browser/js/core/drawingApp.js #findNearestSegmentWithIdx (~1793).
  std::optional<SegmentHit> findNearestSegment(const Lines& lines, double x,
                                               double y, double threshold) {
    double bestDist = threshold;
    std::optional<SegmentHit> best;
    for (std::size_t li = lines.size(); li-- > 0;) {
      const std::vector<Point>& pts = lines[li].points;
      for (std::size_t j = 0; j + 1 < pts.size(); ++j) {
        const double d = distToSegment(x, y, pts[j], pts[j + 1]);
        if (d < bestDist) {
          bestDist = d;
          best = SegmentHit{static_cast<int>(li), static_cast<int>(j),
                            static_cast<int>(j + 1)};
        }
      }
    }
    return best;
  }

  // Port of browser/js/core/drawingApp.js #rotateSelectedLine rotation (~1857).
  void rotatePoints(std::vector<Point>& points, double cx, double cy,
                    double angle) {
    const double cos = std::cos(angle);
    const double sin = std::sin(angle);
    for (Point& p : points) {
      const double dx = p.x - cx;
      const double dy = p.y - cy;
      p.x = cx + dx * cos - dy * sin;
      p.y = cy + dx * sin + dy * cos;
    }
  }

  // Port of the bbox-center pivot in #rotateSelectedLine (~1844).
  Point boundingBoxCenter(const std::vector<Point>& points) {
    if (points.empty()) return Point{0.0, 0.0};
    double minX = points[0].x, maxX = points[0].x;
    double minY = points[0].y, maxY = points[0].y;
    for (const Point& p : points) {
      minX = std::min(minX, p.x);
      maxX = std::max(maxX, p.x);
      minY = std::min(minY, p.y);
      maxY = std::max(maxY, p.y);
    }
    return Point{(minX + maxX) / 2.0, (minY + maxY) / 2.0};
  }

}
