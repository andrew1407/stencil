// The coverage stroke's work, counted rather than timed: the always-run half of the stroke
// speed-up bench (benchGeometry.test.cpp), over the same scene and against the same stamper.
// Pixel visits, not milliseconds, so the ratio holds on any machine and under the sanitizers.
#include "doctest.h"

#include "discStamp.hpp"
#include "rasterize.hpp"
#include "strokeCoverage.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace stencil::core;

TEST_CASE("the coverage stroke examines a tenth of the pixels a disc stamper does") {
  const Line ln = discStamp::scene();
  const coverage::StrokeWork pass = coverage::strokeWork(
      discStamp::SCENE_W, discStamp::SCENE_H, ln.points, false, ln.thickness, nullptr);
  const std::size_t naive = discStamp::visits(ln);
  MESSAGE("pixel visits: coverage pass=" << pass.visits << " (" << pass.blends
                                         << " blends)  disc stamper=" << naive);
  CHECK(pass.blends > 0);
  CHECK(pass.blends <= pass.visits);
  CHECK(pass.visits * 10 <= naive);  // O(length x thickness) against O(length x thickness^2)
}

TEST_CASE("strokeWork counts the pass rasterizeLine draws: one blend per inked pixel") {
  const int w = 64, h = 48;
  Line ln = discStamp::scene();
  ln.points = {{4, 4}, {60, 40}, {10, 44}};
  ln.thickness = 6.0;
  ln.color = "#3366ff";
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 4, 0);
  rasterizeLine(buf.data(), w, h, ln);
  std::size_t inked = 0;
  for (std::size_t i = 3; i < buf.size(); i += 4) inked += buf[i] > 0 ? 1 : 0;
  const coverage::StrokeWork pass = coverage::strokeWork(w, h, ln.points, false, ln.thickness, nullptr);
  CHECK(inked > 0);
  CHECK(pass.blends >= inked);  // a blend under half an alpha step leaves its pixel untouched
  CHECK(pass.blends <= inked + inked / 10);
}
