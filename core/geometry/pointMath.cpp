#include "pointMath.hpp"
#include <algorithm>
#include <cmath>

namespace stencil::core {

  // Clamps the projection parameter t to [0, 1] so the result is distance to the
  // segment, not the infinite line. A zero-length segment degenerates to point
  // distance.
  double distToSegment(double px, double py, const Point& a, const Point& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq == 0.0) return std::hypot(px - a.x, py - a.y);
    double t = ((px - a.x) * dx + (py - a.y) * dy) / lenSq;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(px - (a.x + t * dx), py - (a.y + t * dy));
  }

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

  void flipPoints(std::vector<Point>& points, bool horizontal, double cx,
                  double cy) {
    for (Point& p : points) {
      if (horizontal)
        p.x = 2.0 * cx - p.x;
      else
        p.y = 2.0 * cy - p.y;
    }
  }

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
