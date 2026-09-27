// Point markers (raster/markers): the row pass is byte-equal to box-scanning each marker's disc
// then ring in point order, overlapping and translucent ones included, while large overlapping
// markers cost rows x markers rather than markers x area; the size clamps at MAX_STROKE_THICKNESS
// as a stroke does, and a marker that cannot reach the image costs nothing.
#include "doctest.h"
#include "markers.hpp"
#include "pixelBlend.hpp"
#include "rasterize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace stencil::core;

namespace {
  using Buf = std::vector<std::uint8_t>;

  // The box scan the chord scan replaced, kept as the oracle.
  void boxDisc(Buf& b, int w, int h, double cx, double cy, double r, const Rgba& c) {
    const double rIn = r - 0.5, rInSq = rIn > 0.0 ? rIn * rIn : -1.0, rOutSq = (r + 0.5) * (r + 0.5);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const double dx = (x + 0.5) - cx, dy = (y + 0.5) - cy, dsq = dx * dx + dy * dy;
        if (dsq >= rOutSq) continue;
        const double cov = dsq <= rInSq ? 1.0 : r + 0.5 - std::sqrt(dsq);
        if (cov > 0.0) blend::blendPixel(b.data(), w, h, x, y, c, cov);
      }
  }

  void boxRing(Buf& b, int w, int h, double cx, double cy, double r, const Rgba& c) {
    const double half = 1.0, out = (r + half) * (r + half);
    const double in = r - half > 0.0 ? (r - half) * (r - half) : -1.0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const double dx = (x + 0.5) - cx, dy = (y + 0.5) - cy, dsq = dx * dx + dy * dy;
        if (dsq >= out || dsq <= in) continue;
        const double cov = std::clamp(half - std::abs(std::sqrt(dsq) - r), 0.0, 1.0);
        if (cov > 0.0) blend::blendPixel(b.data(), w, h, x, y, c, cov);
      }
  }

  Line markerLine(std::vector<Point> pts, double size) {
    Line l;
    l.points = std::move(pts);
    l.color = "transparent";
    l.pointColor = "#ff000080";
    l.pointSize = size;
    return l;
  }

  Buf drawn(int w, int h, const Line& l) {
    Buf b(static_cast<std::size_t>(w) * h * 4, 30);
    rasterizeLine(b.data(), w, h, l);
    return b;
  }

  // A varied backdrop, so a blend that runs once too often or too rarely shows in every channel.
  Buf backdrop(int w, int h) {
    Buf b(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<std::uint8_t>((i * 37 + i / 7) % 251);
    return b;
  }

  // The oracle: every marker box-scanned whole, disc then ring, in point order.
  Buf stamped(int w, int h, const std::vector<Point>& pts, double size, const Rgba& fill) {
    Buf b = backdrop(w, h);
    for (const Point& p : pts) {
      boxDisc(b, w, h, p.x, p.y, size, fill);
      boxRing(b, w, h, p.x, p.y, size, Rgba{0, 0, 0, 255});
    }
    return b;
  }

  // A deterministic crowd: repeats, near-misses, off-image centres.
  std::vector<Point> crowd(int n, int w, int h) {
    std::vector<Point> pts;
    std::uint32_t s = 12345;
    const auto next = [&s](int m) { s = s * 1664525u + 1013904223u; return static_cast<int>((s >> 8) % m); };
    for (int i = 0; i < n; ++i) pts.push_back({next(w + 20) - 10 + next(4) * 0.25, next(h + 20) - 10 + next(4) * 0.25});
    pts.push_back(pts[3]);
    pts.push_back(pts[3]);
    return pts;
  }
}  // namespace

TEST_CASE("markers: the chord scan paints exactly what a box scan paints") {
  const int w = 37, h = 29;
  const Rgba fill{255, 0, 0, 128}, ring{0, 0, 0, 255};
  const double sizes[] = {0.3, 0.5, 1.0, 1.7, 2.5, 4.0, 6.25, 11.0, 23.5, 60.0};
  const Point at[] = {{10.2, 9.7}, {0.0, 0.0}, {36.5, 28.9}, {-3.0, 14.0}, {18.5, -5.5}, {40.0, 31.0}};
  for (double s : sizes)
    for (const Point& p : at) {
      Buf want(static_cast<std::size_t>(w) * h * 4, 30);
      boxDisc(want, w, h, p.x, p.y, s, fill);
      boxRing(want, w, h, p.x, p.y, s, ring);
      CHECK_MESSAGE(drawn(w, h, markerLine({p}, s)) == want, "size " << s << " at " << p.x << "," << p.y);
    }
}

TEST_CASE("markers: the size clamps at MAX_STROKE_THICKNESS, as a stroke's does") {
  // 40 000 px off the image: a 1e5 disc would cover it, the clamped 32 768 one cannot reach.
  const Buf blank(8 * 8 * 4, 30);
  CHECK(drawn(8, 8, markerLine({{4.0, 40000.0}}, 1e5)) == blank);
  CHECK(drawn(8, 8, markerLine({{4.0, 40000.0}}, 1e5)) ==
        drawn(8, 8, markerLine({{4.0, 40000.0}}, MAX_STROKE_THICKNESS)));
  // Within reach the clamp changes nothing: both cover the whole image.
  CHECK(drawn(8, 8, markerLine({{4.0, 4.0}}, 1e6)) == drawn(8, 8, markerLine({{4.0, 4.0}}, MAX_STROKE_THICKNESS)));
}

TEST_CASE("markers: a marker that cannot reach the image is skipped, however many there are") {
  std::vector<Point> far(200000, Point{-50000.0, 3.0});
  const Buf blank(16 * 16 * 4, 30);
  CHECK(drawn(16, 16, markerLine(far, 30000.0)) == blank);
  // One just inside reach still paints its rim.
  CHECK(drawn(16, 16, markerLine({{-3.0, 8.0}}, 3.2)) != blank);
}

TEST_CASE("markers: overlapping markers paint exactly what stamping each in turn paints") {
  const int w = 61, h = 47;
  const Rgba fills[] = {{255, 0, 0, 128}, {0, 255, 0, 3}, {32, 80, 255, 255}, {0, 0, 0, 255}, {255, 255, 255, 64}};
  // 17.5 and up lay full-coverage runs longer than a marker blends in place, so both paths run.
  const double sizes[] = {0.7, 2.5, 4.0, 17.5, 40.0, 90.0};
  const std::vector<Point> pts = crowd(40, w, h);
  for (const Rgba& fill : fills)
    for (double size : sizes) {
      Buf got = backdrop(w, h);
      markers::drawMarkers(got.data(), w, h, pts, size, fill, Rgba{0, 0, 0, 255});
      CHECK_MESSAGE(got == stamped(w, h, pts, size, fill), "size " << size << " alpha " << int(fill.a));
    }
}

TEST_CASE("markers: overlapping discs cost their edges and the area once, not markers x area") {
  const int w = 300, h = 200;
  std::vector<Point> pts;
  for (int i = 0; i < 300; ++i) pts.push_back({150.0 + (i % 7) * 3.5, 100.0 + (i % 5) * 2.25});
  const std::size_t naive = pts.size() * w * h;  // stamping blends every pixel a disc covers
  const Rgba ring{0, 0, 0, 255};
  for (const Rgba fill : {Rgba{255, 0, 0, 3}, Rgba{255, 0, 0, 128}, Rgba{255, 0, 0, 255}}) {
    Buf whole = backdrop(w, h), part = backdrop(w, h);
    const std::size_t covers = markers::drawMarkers(whole.data(), w, h, pts, 400.0, fill, ring);
    const std::size_t crosses = markers::drawMarkers(part.data(), w, h, pts, 120.0, fill, ring);
    CHECK_MESSAGE(covers <= 4u * w * h, "alpha " << int(fill.a) << ": " << covers);
    CHECK_MESSAGE(crosses * 8 < naive, "alpha " << int(fill.a) << ": " << crosses);
    CHECK(whole == stamped(w, h, pts, 400.0, fill));
    CHECK(part == stamped(w, h, pts, 120.0, fill));
  }
}
