#include "doctest.h"
#include "rasterize.hpp"

#include <cstdint>
#include <limits>
#include <vector>

using namespace stencil::core;

namespace {
  std::vector<std::uint8_t> blank(int w, int h) {
    return std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, 0);
  }
  std::uint8_t at(const std::vector<std::uint8_t>& b, int w, int x, int y, int ch) {
    return b[(static_cast<std::size_t>(y) * w + x) * 4 + ch];
  }
}  // namespace

TEST_CASE("rasterizeLine strokes a polyline in its colour") {
  const int w = 20, h = 20;
  auto buf = blank(w, h);
  Line line;
  line.points = {{2, 10}, {18, 10}};
  line.color = "red";
  line.thickness = 3;
  line.style = "solid";
  line.markerSize = 0;
  rasterizeLine(buf.data(), w, h, line);

  CHECK(at(buf, w, 10, 10, 0) > 150);  // red on the line
  CHECK(at(buf, w, 10, 10, 1) < 100);  // little green
  CHECK(at(buf, w, 10, 0, 3) == 0);    // far from the line -> untouched
}

TEST_CASE("rasterizeLine fills a locked polygon") {
  const int w = 20, h = 20;
  auto buf = blank(w, h);
  Line area;
  area.points = {{4, 4}, {16, 4}, {16, 16}, {4, 16}};
  area.color = "transparent";   // no stroke
  area.fillColor = "blue";
  area.locked = true;
  area.markerSize = 0;
  rasterizeLine(buf.data(), w, h, area);

  CHECK(at(buf, w, 10, 10, 2) > 150);  // blue interior
  CHECK(at(buf, w, 10, 10, 3) == 255);
  CHECK(at(buf, w, 0, 0, 3) == 0);     // outside the polygon
}

TEST_CASE("rasterizeLine: an off-canvas fill span leaves no spurious edge stripe") {
  const int w = 20, h = 20;
  // A locked, filled rectangle whose interior lies entirely to the RIGHT of the
  // canvas. Its fill spans are off-canvas at every scanline, so nothing is drawn —
  // in particular the right border column (x = w-1) must stay untouched. (A symmetric
  // clamp of both span ends would collapse them onto x = w-1 and paint a stripe.)
  {
    auto buf = blank(w, h);
    Line area;
    area.points = {{200, 4}, {260, 4}, {260, 16}, {200, 16}};
    area.color = "transparent";
    area.fillColor = "blue";
    area.locked = true;
    area.markerSize = 0;
    rasterizeLine(buf.data(), w, h, area);
    for (int y = 0; y < h; ++y) CHECK(at(buf, w, w - 1, y, 3) == 0);  // no stripe at x=19
  }
  // Same, entirely to the LEFT — the left border column (x = 0) must stay clean.
  {
    auto buf = blank(w, h);
    Line area;
    area.points = {{-260, 4}, {-200, 4}, {-200, 16}, {-260, 16}};
    area.color = "transparent";
    area.fillColor = "blue";
    area.locked = true;
    area.markerSize = 0;
    rasterizeLine(buf.data(), w, h, area);
    for (int y = 0; y < h; ++y) CHECK(at(buf, w, 0, y, 3) == 0);  // no stripe at x=0
  }
  // Sanity: a polygon straddling the right edge still fills its on-canvas part up to
  // the clipped border (proves the fix clips, not just "never draws off-canvas").
  {
    auto buf = blank(w, h);
    Line area;
    area.points = {{10, 4}, {40, 4}, {40, 16}, {10, 16}};  // right half off-canvas
    area.color = "transparent";
    area.fillColor = "blue";
    area.locked = true;
    area.markerSize = 0;
    rasterizeLine(buf.data(), w, h, area);
    CHECK(at(buf, w, 15, 10, 2) > 150);      // interior on-canvas still filled
    CHECK(at(buf, w, w - 1, 10, 3) == 255);  // fill reaches the clipped edge
  }
}

TEST_CASE("rasterizeLine: a dashed stroke leaves gaps along the path") {
  const int w = 48, h = 20;
  auto buf = blank(w, h);
  Line line;
  line.points = {{2, 10}, {42, 10}};
  line.color = "red";
  line.thickness = 2;        // dash cycle: 6px on, 4px off
  line.style = "dashed";
  line.markerSize = 0;
  rasterizeLine(buf.data(), w, h, line);

  CHECK(at(buf, w, 4, 10, 3) > 0);    // x≈4 (pos 2) lands on an "ink" dash
  CHECK(at(buf, w, 10, 10, 3) == 0);  // x≈10 (pos 8) lands in a gap -> untouched
}

TEST_CASE("rasterizeLine draws point markers") {
  const int w = 20, h = 20;
  auto buf = blank(w, h);
  Line line;
  line.points = {{10, 10}};
  line.color = "red";
  line.thickness = 2;
  line.markerSize = 4;
  rasterizeLine(buf.data(), w, h, line);
  CHECK(at(buf, w, 10, 10, 3) > 0);  // a marker disc was stamped at the point
}

// Security/robustness: layout coords/sizes are untrusted. Non-finite or absurd
// values must not cast to an out-of-range int (UB) or spin a near-infinite scan/
// step loop (DoS) — the line is skipped, the buffer left untouched, promptly.
TEST_CASE("rasterizeLine is inert on non-finite and astronomically large inputs") {
  const int w = 16, h = 16;
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();

  auto expectUntouched = [&](const Line& line) {
    auto buf = blank(w, h);
    rasterizeLine(buf.data(), w, h, line);  // must return promptly, not hang/crash
    for (std::uint8_t b : buf) CHECK(b == 0);
  };

  {  // a point at 1e18 (finite but out of int range) — used to be UB + a huge loop
    Line line; line.points = {{0, 0}, {1e18, 1e18}}; line.color = "red";
    line.thickness = 2; line.markerSize = 0;
    expectUntouched(line);
  }
  {  // a NaN point
    Line line; line.points = {{nan, nan}, {5, 5}}; line.color = "red";
    line.thickness = 2; line.markerSize = 0;
    expectUntouched(line);
  }
  {  // an infinite thickness
    Line line; line.points = {{2, 2}, {14, 14}}; line.color = "red";
    line.thickness = inf; line.markerSize = 0;
    expectUntouched(line);
  }
  {  // an astronomically large markerSize
    Line line; line.points = {{8, 8}}; line.color = "red";
    line.thickness = 2; line.markerSize = 1e18;
    expectUntouched(line);
  }
}
