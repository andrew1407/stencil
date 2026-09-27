#include "hitTest.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace stencil::core {

  namespace {
    // hitTest.js farFromBox: a hit within `margin` puts (x, y) within `margin` of the bbox, so a
    // rejected line cannot match and topmost-first is untouched. NaN coords fail the compares.
    bool farFromBox(const std::vector<Point>& pts, double x, double y, double margin) {
      double minX = pts[0].x, maxX = minX, minY = pts[0].y, maxY = minY;
      for (const Point& p : pts) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
      }
      return x < minX - margin || x > maxX + margin || y < minY - margin || y > maxY + margin;
    }
  }  // namespace

  bool shouldCloseShape(const std::vector<Point>& points, const Point& click,
                        double pointSize) {
    if (points.size() < 3) return false;
    const Point& first = points.front();
    const double d = std::hypot(click.x - first.x, click.y - first.y);
    return d <= pointSize + CLOSE_SLACK_PX;
  }

  int findLineAt(const Lines& lines, double x, double y, double threshold) {
    const double margin = threshold + 4.0;  // the wider of the two radii
    // Squared radii; a negative radius (or NaN) reaches nothing, as a distance never falls below it.
    const double pointSq = margin >= 0.0 ? margin * margin : -1.0;
    const double segSq = threshold >= 0.0 ? threshold * threshold : -1.0;
    for (std::size_t i = lines.size(); i-- > 0;) {
      const std::vector<Point>& pts = lines[i].points;
      if (pts.empty() || farFromBox(pts, x, y, margin)) continue;

      for (const Point& p : pts) {
        const double dx = p.x - x, dy = p.y - y;
        if (dx * dx + dy * dy <= pointSq) return static_cast<int>(i);
      }

      for (std::size_t j = 0; j + 1 < pts.size(); ++j)
        if (distToSegmentSq(x, y, pts[j], pts[j + 1]) <= segSq)
          return static_cast<int>(i);
    }
    return -1;
  }

  std::optional<PointHit> findNearestPoint(const Lines& lines, double x, double y,
                                           double threshold) {
    if (!(threshold > 0.0)) return std::nullopt;  // no distance is below it
    const double limitSq = threshold * threshold;
    for (std::size_t li = lines.size(); li-- > 0;) {
      const std::vector<Point>& pts = lines[li].points;
      if (pts.empty() || farFromBox(pts, x, y, threshold)) continue;
      for (std::size_t pi = 0; pi < pts.size(); ++pi) {
        const double dx = pts[pi].x - x, dy = pts[pi].y - y;
        if (dx * dx + dy * dy < limitSq)
          return PointHit{static_cast<int>(li), static_cast<int>(pi)};
      }
    }
    return std::nullopt;
  }

  std::optional<int> nearestPointInLine(const std::vector<Point>& points,
                                        double x, double y, double threshold) {
    if (!(threshold > 0.0)) return std::nullopt;
    const double limitSq = threshold * threshold;
    for (std::size_t i = 0; i < points.size(); ++i) {
      const double dx = points[i].x - x, dy = points[i].y - y;
      if (dx * dx + dy * dy < limitSq) return static_cast<int>(i);
    }
    return std::nullopt;
  }

  std::optional<SegmentHit> findNearestSegment(const Lines& lines, double x,
                                               double y, double threshold) {
    if (!(threshold > 0.0)) return std::nullopt;  // no distance is below it
    double bestSq = threshold * threshold;
    std::optional<SegmentHit> best;
    for (std::size_t li = lines.size(); li-- > 0;) {
      const std::vector<Point>& pts = lines[li].points;
      if (pts.empty() || farFromBox(pts, x, y, threshold)) continue;
      for (std::size_t j = 0; j + 1 < pts.size(); ++j) {
        const double d = distToSegmentSq(x, y, pts[j], pts[j + 1]);
        if (d < bestSq) {
          bestSq = d;
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
