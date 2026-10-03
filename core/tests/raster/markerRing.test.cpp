// A marker's ring contrasts with its own fill (raster/markers ringFor): black on a light fill,
// white on a dark one, split at MARKER_RING.darkFromLuma in common/config/constants.json.
#include "doctest.h"
#include "markers.hpp"
#include "rasterize.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace stencil::core;

namespace {
  int canonicalThreshold() {
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    while (!fs::exists(p / "CLAUDE.md") && p.has_parent_path() && p.parent_path() != p) p = p.parent_path();
    std::stringstream src;
    src << std::ifstream(p / "common/config/constants.json").rdbuf();
    const std::string json = src.str();
    const std::size_t row = json.find("\"darkFromLuma\"", json.find("\"MARKER_RING\""));
    if (row == std::string::npos) return -1;
    return std::stoi(json.substr(json.find(':', row) + 1));
  }

  // The ring pixel straight above a 3 px marker at (10.5, 10.5) on a grey ground.
  int ringRed(const char* colour) {
    std::vector<std::uint8_t> buf(20 * 20 * 4, 128);
    Line l;
    l.points = {{10.5, 10.5}};
    l.color = colour;
    l.pointSize = 3;
    rasterizeLine(buf.data(), 20, 20, l);
    return buf[(7 * 20 + 10) * 4];
  }
}  // namespace

TEST_CASE("the ring threshold is MARKER_RING.darkFromLuma") {
  CHECK(markers::RING_DARK_FROM_LUMA == canonicalThreshold());
}

TEST_CASE("ringFor: black on a light fill, white on a dark one") {
  CHECK(markers::ringFor(Rgba{255, 255, 0}).r == 0);      // yellow
  CHECK(markers::ringFor(Rgba{255, 255, 255}).r == 0);    // white
  CHECK(markers::ringFor(Rgba{255, 0, 0}).r == 255);      // red, luma 54
  CHECK(markers::ringFor(Rgba{30, 99, 200}).r == 255);    // blue
  CHECK(markers::ringFor(Rgba{0, 0, 0}).g == 255);
  CHECK(markers::ringFor(Rgba{128, 128, 128}).r == 0);    // luma 128: light, as render.js ringFor
  CHECK(markers::ringFor(Rgba{127, 127, 127}).r == 255);
}

TEST_CASE("rasterizeLine rings a yellow point in black and a red one in white") {
  CHECK(ringRed("#ffff00") < 40);
  CHECK(ringRed("#ff0000") > 215);
}
