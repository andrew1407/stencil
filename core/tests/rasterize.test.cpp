#include "doctest.h"
#include "rasterize.hpp"

#include <cstdint>
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
