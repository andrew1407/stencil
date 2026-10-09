// abi/linesCodec.hpp decodeLines: the layout caps it shares with browser/js/core/layout.js
// sanitizeLines, and counts no hostile buffer can push through an out-of-range int cast.
#include "doctest.h"
#include "linesCodec.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace stencil::core;

namespace {
  struct Encoded {
    std::vector<double> nums;
    std::vector<std::uint8_t> text;
    Lines decode() const {
      return abi::decodeLines(nums.data(), static_cast<int>(nums.size()), text.data(),
                              static_cast<int>(text.size()));
    }
  };

  Encoded encode(const Lines& lines) {
    const abi::LinesSize sz = abi::linesSize(lines);
    Encoded e{std::vector<double>(static_cast<std::size_t>(sz.nums)),
              std::vector<std::uint8_t>(static_cast<std::size_t>(sz.text) + 1)};
    abi::encodeLines(lines, e.nums.data(), e.text.data());
    e.text.pop_back();
    return e;
  }

  Line withPoints(int n) {
    Line l;
    l.color = "#fff";
    l.points.assign(static_cast<std::size_t>(n), Point{1, 2});
    return l;
  }

  // One LIMITS value out of common/config/constants.json, the canon; NaN when missing.
  double canonicalLimit(const std::string& key) {
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    while (!fs::exists(p / "CLAUDE.md") && p.has_parent_path() && p.parent_path() != p)
      p = p.parent_path();
    std::stringstream src;
    src << std::ifstream(p / "common/config/constants.json").rdbuf();
    const std::string json = src.str();
    const std::size_t at = json.find("\"" + key + "\"", json.find("\"LIMITS\""));
    if (at == std::string::npos) return std::numeric_limits<double>::quiet_NaN();
    return std::strtod(json.c_str() + json.find(':', at) + 1, nullptr);
  }
}  // namespace

TEST_CASE("drift: the layout caps match LIMITS in common/config/constants.json") {
  CHECK(canonicalLimit("layoutLinesMax") == abi::MAX_LAYOUT_LINES);
  CHECK(canonicalLimit("layoutLinePointsMax") == abi::MAX_LINE_POINTS);
  CHECK(canonicalLimit("layoutPointsMax") == abi::MAX_LAYOUT_POINTS);
}

TEST_CASE("decodeLines cuts a line at the per-line cap and stops at the total") {
  Lines lines(12, withPoints(abi::MAX_LINE_POINTS - 1));
  lines[0] = withPoints(abi::MAX_LINE_POINTS + 7);  // cut to the per-line cap
  lines[0].style = "dashed";
  const Lines back = encode(lines).decode();
  // 100000 + 9 * 99999 = 999991 points, so the eleventh line keeps the last 9 and the rest go.
  REQUIRE(back.size() == 11);
  CHECK(back[0].points.size() == static_cast<std::size_t>(abi::MAX_LINE_POINTS));
  CHECK(back[0].style == "dashed");  // the text stays in step past the skipped points
  CHECK(back[1].points.size() == static_cast<std::size_t>(abi::MAX_LINE_POINTS - 1));
  CHECK(back[10].points.size() == 9);
  CHECK(back[10].color == "#fff");
}

TEST_CASE("decodeLines keeps at most MAX_LAYOUT_LINES lines") {
  const Lines back = encode(Lines(abi::MAX_LAYOUT_LINES + 5, withPoints(0))).decode();
  CHECK(back.size() == static_cast<std::size_t>(abi::MAX_LAYOUT_LINES));
}

TEST_CASE("decodeLines stops cleanly on NaN, negative and astronomical counts") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const Encoded good = encode(Lines{withPoints(2), withPoints(3)});
  const auto patched = [&](std::size_t at, double v) {
    Encoded e = good;
    e.nums[at] = v;
    return e.decode().size();
  };
  CHECK(good.decode().size() == 2);
  CHECK(patched(0, nan) == 0);    // line count
  CHECK(patched(0, -3) == 0);
  CHECK(patched(0, 1e300) == 2);  // clamped to the cap, then bounded by the buffer
  CHECK(patched(1, 1e300) == 0);  // first line's point count
  CHECK(patched(1, nan) == 0);
  CHECK(patched(6, 1e300) == 0);  // first line's color length
  CHECK(patched(6, -1) == 0);
  CHECK(patched(15, nan) == 1);   // second line's point count: the first survives
}

TEST_CASE("a line's name and hidden flag survive the round trip") {
  Line named = withPoints(2);
  named.name = "Roof ridge";
  named.hidden = true;
  const Lines back = encode(Lines{named, withPoints(1)}).decode();
  REQUIRE(back.size() == 2);
  CHECK(back[0].name == "Roof ridge");
  CHECK(back[0].hidden);
  CHECK(back[0].points.size() == 2);
  CHECK(back[1].name.empty());
  CHECK_FALSE(back[1].hidden);
  CHECK(back[1].color == "#fff");
}
