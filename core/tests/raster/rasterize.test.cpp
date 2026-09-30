#include "doctest.h"
#include "rasterize.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace stencil::core;

namespace {
  std::vector<std::uint8_t> blank(int w, int h, std::uint8_t v = 0) {
    return std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, v);
  }
  std::uint8_t at(const std::vector<std::uint8_t>& b, int w, int x, int y, int ch) {
    return b[(static_cast<std::size_t>(y) * w + x) * 4 + ch];
  }
  Line mk(std::vector<Point> pts, const char* color, double thickness, double pointSize = 0,
          const char* style = "solid") {
    Line l;
    l.points = std::move(pts);
    l.color = color;
    l.thickness = thickness;
    l.pointSize = pointSize;
    l.style = style;
    return l;
  }
  Line area(std::vector<Point> pts) {  // a locked, stroke-less blue fill
    Line l = mk(std::move(pts), "transparent", 2);
    l.fillColor = "blue";
    l.locked = true;
    return l;
  }
  std::vector<std::uint8_t> drawn(int w, int h, const Line& l, std::uint8_t bg = 0) {
    auto buf = blank(w, h, bg);
    rasterizeLine(buf.data(), w, h, l);
    return buf;
  }
  // The `[on, off]` of one STROKE_DASH row in common/config/constants.json, the canon.
  std::vector<double> canonicalDash(const std::string& style) {
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    while (!fs::exists(p / "CLAUDE.md") && p.has_parent_path() && p.parent_path() != p)
      p = p.parent_path();
    std::stringstream src;
    src << std::ifstream(p / "common/config/constants.json").rdbuf();
    const std::string json = src.str();
    const std::size_t table = json.find("\"STROKE_DASH\"");
    const std::size_t row = json.find("\"" + style + "\"", table);
    if (table == std::string::npos || row == std::string::npos) return {};
    const char* cur = json.c_str() + json.find('[', row) + 1;
    char* end = nullptr;
    const double on = std::strtod(cur, &end);
    const double off = std::strtod(end + std::strspn(end, " ,"), nullptr);
    return {on, off};
  }
}  // namespace

TEST_CASE("rasterizeLine strokes a polyline in its colour") {
  const auto buf = drawn(20, 20, mk({{2, 10}, {18, 10}}, "red", 3));
  CHECK(at(buf, 20, 10, 10, 0) > 150);  // red on the line
  CHECK(at(buf, 20, 10, 10, 1) < 100);  // little green
  CHECK(at(buf, 20, 10, 0, 3) == 0);    // far from the line -> untouched
}

// A 3 px line on y = 10 reaches y 8.5..11.5: the rows centred 1.5 px off it are half inked.
// Stamping discs used to blend each rim pixel several times, nearly to opaque.
TEST_CASE("an opaque stroke's anti-aliased rim is its geometric coverage") {
  const auto buf = drawn(20, 20, mk({{2, 10}, {18, 10}}, "red", 3));
  CHECK(at(buf, 20, 10, 9, 3) == 255);
  CHECK(at(buf, 20, 10, 8, 3) == 128);
  CHECK(at(buf, 20, 10, 11, 3) == 128);
  CHECK(at(buf, 20, 10, 7, 3) == 0);
}

// Canvas strokes a path once, so a #rrggbbaa stroke over white is one blend everywhere: mid-
// span, at a corner where two segments overlap, and where a closed path retraces itself.
TEST_CASE("a translucent stroke blends once, never accumulating toward opaque") {
  const std::uint8_t once = 127;  // white green under one 0x80 blend: 255 * (255 - 128) / 255
  const auto bent = drawn(40, 40, mk({{4, 10}, {30, 10}, {30, 36}}, "#ff000080", 6), 255);
  CHECK(at(bent, 40, 16, 10, 1) == once);
  CHECK(at(bent, 40, 30, 10, 1) == once);
  CHECK(at(bent, 40, 30, 22, 1) == once);
  Line back = mk({{4, 20}, {36, 20}}, "#ff000080", 4);
  back.locked = true;  // two points, closed: the segment runs out and back
  CHECK(at(drawn(40, 40, back, 255), 40, 20, 20, 1) == once);
}

TEST_CASE("rasterizeLine fills a locked polygon") {
  const auto buf = drawn(20, 20, area({{4, 4}, {16, 4}, {16, 16}, {4, 16}}));
  CHECK(at(buf, 20, 10, 10, 2) > 150);  // blue interior
  CHECK(at(buf, 20, 10, 10, 3) == 255);
  CHECK(at(buf, 20, 0, 0, 3) == 0);     // outside the polygon
}

// A fill wholly off one side must not stripe that border column (a symmetric clamp would).
TEST_CASE("rasterizeLine: an off-canvas fill span leaves no spurious edge stripe") {
  const int w = 20, h = 20;
  const auto right = drawn(w, h, area({{200, 4}, {260, 4}, {260, 16}, {200, 16}}));
  const auto left = drawn(w, h, area({{-260, 4}, {-200, 4}, {-200, 16}, {-260, 16}}));
  for (int y = 0; y < h; ++y) {
    CHECK(at(right, w, w - 1, y, 3) == 0);
    CHECK(at(left, w, 0, y, 3) == 0);
  }
  // Straddling the right edge still fills up to the clipped border.
  const auto straddle = drawn(w, h, area({{10, 4}, {40, 4}, {40, 16}, {10, 16}}));
  CHECK(at(straddle, w, 15, 10, 2) > 150);
  CHECK(at(straddle, w, w - 1, 10, 3) == 255);
}

// Dashes are px along the path whatever the thickness, round-capped, phase kept at corners.
TEST_CASE("rasterizeLine: a dashed stroke leaves gaps along the path") {
  const auto buf = drawn(48, 20, mk({{2, 10}, {42, 10}}, "red", 2, 0, "dashed"));
  CHECK(at(buf, 48, 4, 10, 3) > 0);    // pos 2.5: the first 10 px dash
  CHECK(at(buf, 48, 15, 10, 3) == 0);  // pos 13.5: the 5 px gap, clear of both caps
  CHECK(at(buf, 48, 20, 10, 3) > 0);   // pos 18.5: the second dash
  const auto dots = drawn(48, 20, mk({{2, 10}, {42, 10}}, "red", 2, 0, "dotted"));
  CHECK(at(dots, 48, 3, 10, 3) == 255);  // pos 1.5: a 2 px dot
  CHECK(at(dots, 48, 6, 10, 3) == 0);    // pos 4.5: its gap
}

TEST_CASE("drift: the dash table matches STROKE_DASH in common/config/constants.json") {
  const auto dashed = canonicalDash("dashed"), dotted = canonicalDash("dotted");
  REQUIRE_MESSAGE(dashed.size() == 2, "constants.json lost STROKE_DASH.dashed (or moved)");
  REQUIRE_MESSAGE(dotted.size() == 2, "constants.json lost STROKE_DASH.dotted");
  CHECK(dashed[0] == DASHED.on);
  CHECK(dashed[1] == DASHED.off);
  CHECK(dotted[0] == DOTTED.on);
  CHECK(dotted[1] == DOTTED.off);
}

TEST_CASE("a stroke wider than MAX_STROKE_THICKNESS burns at the cap") {
  // Centred 17000 px above the image: only an uncapped half-width (> 17000) reaches it.
  const Line capped = mk({{-100, -17000}, {100, -17000}}, "red", MAX_STROKE_THICKNESS);
  const Line wide = mk({{-100, -17000}, {100, -17000}}, "red", 40000);
  CHECK(drawn(16, 16, wide) == drawn(16, 16, capped));
  CHECK(at(drawn(16, 16, wide), 16, 8, 0, 3) == 0);
}

TEST_CASE("rasterizeLine draws points") {
  CHECK(at(drawn(20, 20, mk({{10, 10}}, "red", 2, 4)), 20, 10, 10, 3) > 0);
  // An invisible stroke still marks points that carry their own colour, none without.
  Line line = mk({{10, 10}}, "transparent", 2, 4);
  CHECK(at(drawn(20, 20, line), 20, 10, 10, 3) == 0);
  line.pointColor = "blue";
  CHECK(at(drawn(20, 20, line), 20, 10, 10, 2) > 150);
}

// Untrusted coords/sizes: non-finite or absurd values must not cast out of int range (UB) or
// spin a near-infinite scan loop (DoS) - the line is skipped, promptly.
TEST_CASE("rasterizeLine is inert on non-finite and astronomically large inputs") {
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const auto untouched = [](const Line& line) {
    for (std::uint8_t b : drawn(16, 16, line)) CHECK(b == 0);
  };
  untouched(mk({{0, 0}, {1e18, 1e18}}, "red", 2));  // finite but out of int range
  untouched(mk({{nan, nan}, {5, 5}}, "red", 2));
  untouched(mk({{2, 2}, {14, 14}}, "red", inf));
  untouched(mk({{8, 8}}, "red", 2, 1e18));
}

// -- Independent point colour (Line::pointColor) ----------------------
// Twin of browser/tests/core/draw/pointColor.test.js. An EMPTY pointColor keeps the old behaviour.

TEST_CASE("points default to the stroke colour when pointColor is unset") {
  const Line line = mk({{10, 10}}, "red", 2, 3);
  CHECK(line.pointColor.empty());  // the back-compatible default
  const auto buf = drawn(20, 20, line);
  CHECK(at(buf, 20, 10, 10, 0) > 150);  // point is red, like the stroke
  CHECK(at(buf, 20, 10, 10, 2) < 100);
}

TEST_CASE("pointColor colours the points independently of the stroke") {
  Line line = mk({{4, 12}, {20, 12}}, "red", 3, 3);
  line.pointColor = "blue";
  const auto buf = drawn(24, 24, line);
  CHECK(at(buf, 24, 4, 12, 2) > 150);  // at an endpoint the point disc wins: blue
  CHECK(at(buf, 24, 4, 12, 0) < 100);
  CHECK(at(buf, 24, 12, 12, 0) > 150);  // mid-span the stroke is still red
  CHECK(at(buf, 24, 12, 12, 2) < 100);
}

TEST_CASE("pointColorOr resolves the fallback in one place") {
  Line line;
  line.color = "#FFFF00";
  CHECK(pointColorOr(line) == "#FFFF00");  // unset → stroke
  line.pointColor = "#00FF00";
  CHECK(pointColorOr(line) == "#00FF00");  // set → its own
}

TEST_CASE("an unparseable pointColor falls back to the stroke, never drops the point") {
  Line line = mk({{10, 10}}, "red", 2, 3);
  line.pointColor = "not-a-colour";
  const auto buf = drawn(20, 20, line);
  CHECK(at(buf, 20, 10, 10, 3) > 0);    // a point was still drawn
  CHECK(at(buf, 20, 10, 10, 0) > 150);  // in the stroke colour
}
