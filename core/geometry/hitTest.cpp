#include "hitTest.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace stencil::core {

  bool shouldCloseShape(const std::vector<Point>& points, const Point& click,
                        double pointSize) {
    if (points.size() < 3) return false;
    const Point& first = points.front();
    const double d = std::hypot(click.x - first.x, click.y - first.y);
    return d <= pointSize + 8.0;
  }

  int findLineAt(const Lines& lines, double x, double y, double threshold) {
    const double margin = threshold + 4.0;  // the wider of the two radii
    for (std::size_t i = lines.size(); i-- > 0;) {
      const std::vector<Point>& pts = lines[i].points;
      if (pts.empty()) continue;

      // Bbox reject: a hit implies (x, y) within `margin` of the bbox, so a rejected line
      // cannot match and topmost-first is untouched. Non-finite coords fail the compares.
      double minX = pts[0].x, maxX = minX, minY = pts[0].y, maxY = minY;
      for (const Point& p : pts) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
      }
      if (x < minX - margin || x > maxX + margin || y < minY - margin ||
          y > maxY + margin)
        continue;

      for (const Point& p : pts)
        if (std::hypot(p.x - x, p.y - y) <= margin)
          return static_cast<int>(i);

      for (std::size_t j = 0; j + 1 < pts.size(); ++j)
        if (distToSegment(x, y, pts[j], pts[j + 1]) <= threshold)
          return static_cast<int>(i);
    }
    return -1;
  }

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

  std::optional<int> nearestPointInLine(const std::vector<Point>& points,
                                        double x, double y, double threshold) {
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (std::hypot(points[i].x - x, points[i].y - y) < threshold)
        return static_cast<int>(i);
    }
    return std::nullopt;
  }

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

  HoldTarget holdDrawTarget(const Lines& lines, double x, double y,
                            double pointThreshold, double segThreshold) {
    if (auto p = findNearestPoint(lines, x, y, pointThreshold))
      return HoldTarget{HoldTargetKind::CONTINUE_POINT, p->lineIdx, p->ptIdx, -1};
    if (auto s = findNearestSegment(lines, x, y, segThreshold))
      return HoldTarget{HoldTargetKind::INSERT_SEGMENT, s->lineIdx, s->ptIdx1, s->ptIdx2};
    return HoldTarget{HoldTargetKind::NEW_LINE, -1, -1, -1};
  }

}
