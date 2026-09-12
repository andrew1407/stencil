#include "doctest.h"
#include "cliApi.h"

#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("stencil_cli_parseColor maps names/hex and rejects junk") {
  int r = -1, g = -1, b = -1, a = -1;
  CHECK(stencil_cli_parseColor("red", &r, &g, &b, &a) == 1);
  CHECK((r == 255 && g == 0 && b == 0 && a == 255));
  CHECK(stencil_cli_parseColor("#0000ff", &r, &g, &b, &a) == 1);
  CHECK((r == 0 && g == 0 && b == 255));
  CHECK(stencil_cli_parseColor("notacolour", &r, &g, &b, &a) == 0);
}

TEST_CASE("stencil_cli_namedPageSize / defaultBlankSizePx") {
  double wcm = 0, hcm = 0;
  CHECK(stencil_cli_namedPageSize("A4", &wcm, &hcm) == 1);
  CHECK(wcm == doctest::Approx(21.0));
  CHECK(stencil_cli_namedPageSize("B5", &wcm, &hcm) == 1);  // full ISO table
  CHECK(wcm == doctest::Approx(17.6));
  CHECK(hcm == doctest::Approx(25.0));
  CHECK(stencil_cli_namedPageSize("nope", &wcm, &hcm) == 0);

  int pw = 0, ph = 0;
  stencil_cli_defaultBlankSizePx(21.0, 29.7, 96.0, &pw, &ph);
  CHECK(pw > 0);
  CHECK(ph > pw);
}

TEST_CASE("stencil_cli_pageFormats lists the canonical names in order") {
  const std::string names = stencil_cli_pageFormats();
  CHECK(names ==
        "A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 A10 "
        "B0 B1 B2 B3 B4 B5 B6 B7 B8 B9 B10 "
        "C0 C1 C2 C3 C4 C5 C6 C7 C8 C9 C10");
}

TEST_CASE("stencil_cli_resolveCrop yields a clamped pixel rect") {
  int x = -1, y = -1, w = -1, h = -1;
  const int ok = stencil_cli_resolveCrop("x1 = 0px x2 = 100px y1 = 0px y2 = 50px",
                                         200, 200, 10, 10, 21, 29.7, 0,
                                         &x, &y, &w, &h);
  CHECK(ok == 1);
  CHECK((x == 0 && y == 0 && w == 100 && h == 50));

  CHECK(stencil_cli_resolveCrop("z = 1", 200, 200, 10, 10, 21, 29.7, 0,
                                &x, &y, &w, &h) == 0);
}

TEST_CASE("stencil_cli rotate helpers") {
  CHECK(stencil_cli_normalizeQuarters(-1) == 3);
  int ow = 0, oh = 0;
  stencil_cli_rotatedDims(4, 2, 1, &ow, &oh);
  CHECK((ow == 2 && oh == 4));
}

TEST_CASE("stencil_cli_fillRGBA + applyFilter bw greyscales") {
  std::vector<std::uint8_t> px(4, 0);
  stencil_cli_fillRGBA(px.data(), 1, 100, 150, 200, 255);
  stencil_cli_applyFilter("bw", px.data(), 1, 0, 0, 0);
  CHECK(px[0] == px[1]);
  CHECK(px[1] == px[2]);
  CHECK(px[3] == 255);  // alpha preserved
}

TEST_CASE("stencil_cli_applyFilter invert / applyContour") {
  std::vector<std::uint8_t> px = {12, 34, 56, 10};
  stencil_cli_applyFilter("invert", px.data(), 1, 0, 0, 0);
  CHECK(px[0] == 243);
  CHECK(px[1] == 221);
  CHECK(px[2] == 199);
  CHECK(px[3] == 10);  // alpha preserved

  // "contour" through the dimensionless entry point is a no-op...
  std::vector<std::uint8_t> same = {1, 2, 3, 4};
  const std::vector<std::uint8_t> before = same;
  stencil_cli_applyFilter("contour", same.data(), 1, 0, 0, 0);
  CHECK(same == before);

  // ...the dimensioned one edge-detects: a uniform 2x2 has no gradients.
  std::vector<std::uint8_t> buf = {100, 150, 200, 1, 100, 150, 200, 2,
                                   100, 150, 200, 3, 100, 150, 200, 4};
  stencil_cli_applyContour(buf.data(), 2, 2);
  for (int i = 0; i < 4; ++i) {
    CHECK(buf[i * 4 + 0] == 255);  // all white
    CHECK(buf[i * 4 + 3] == i + 1);  // alpha preserved
  }
  stencil_cli_applyContour(nullptr, 2, 2);  // null data must not crash
}

TEST_CASE("stencil_cli_rasterizeLine draws into the buffer") {
  const int w = 16, h = 16;
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 4, 0);
  const double pts[] = {2, 8, 14, 8};
  stencil_cli_rasterizeLine(buf.data(), w, h, pts, 2, "red", 3, 0, "solid", 0,
                            "transparent", "");
  CHECK(buf[(8 * w + 8) * 4 + 0] > 100);  // red along the stroke
}

// The point colour rides the LAST parameter; NULL and "" both mean "inherit `color`", so
// a caller built before the field existed keeps its exact rendering.
TEST_CASE("stencil_cli_rasterizeLine takes an independent point colour") {
  const int w = 24, h = 24;
  const double pts[] = {4, 12, 20, 12};
  auto draw = [&](const char* pointColor) {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 4, 0);
    stencil_cli_rasterizeLine(buf.data(), w, h, pts, 2, "red", 3, 3, "solid", 0,
                              "transparent", pointColor);
    return buf;
  };
  const auto blue = draw("blue");
  CHECK(blue[(12 * w + 4) * 4 + 2] > 150);   // endpoint point is blue
  CHECK(blue[(12 * w + 4) * 4 + 0] < 100);
  CHECK(blue[(12 * w + 12) * 4 + 0] > 150);  // stroke mid-span still red

  const auto inherited = draw("");
  CHECK(inherited[(12 * w + 4) * 4 + 0] > 150);   // point inherits red
  CHECK(inherited[(12 * w + 4) * 4 + 2] < 100);
  const auto nulled = draw(nullptr);              // NULL is the same as ""
  CHECK(nulled[(12 * w + 4) * 4 + 0] > 150);
  CHECK(nulled[(12 * w + 4) * 4 + 2] < 100);
}

// Pins the ownership half of the buffer contract stated at the top of cliApi.h:
// every out-pointer is optional, and a failing call leaves the caller's slots alone.
TEST_CASE("cliApi contract: NULL out-pointers are tolerated, failures write nothing") {
  CHECK(stencil_cli_parseColor("red", nullptr, nullptr, nullptr, nullptr) == 1);
  CHECK(stencil_cli_namedPageSize("A4", nullptr, nullptr) == 1);
  stencil_cli_defaultBlankSizePx(21.0, 29.7, 96.0, nullptr, nullptr);
  stencil_cli_rotatedDims(3, 7, 1, nullptr, nullptr);
  CHECK(stencil_cli_colorNameAt(0, nullptr) != nullptr);
  CHECK(stencil_cli_parseDuration("week", nullptr) == 1);

  int r = -7, g = -7, b = -7, a = -7;
  CHECK(stencil_cli_parseColor("notacolour", &r, &g, &b, &a) == 0);
  CHECK((r == -7 && g == -7 && b == -7 && a == -7));

  double wcm = -7, hcm = -7;
  CHECK(stencil_cli_namedPageSize("nope", &wcm, &hcm) == 0);
  CHECK((wcm == -7 && hcm == -7));

  long long ms = -7;
  CHECK(stencil_cli_parseDuration("banana", &ms) == 0);
  CHECK(ms == -7);

  int cx = -7, cy = -7, cw = -7, ch = -7;
  CHECK(stencil_cli_resolveCrop("nonsense", 100, 100, 1, 1, 21, 29.7, 0,
                                &cx, &cy, &cw, &ch) == 0);
  CHECK((cx == -7 && cy == -7 && cw == -7 && ch == -7));

  // A crop rect hanging off the source fills dst in full, zeroing what is outside.
  const std::vector<std::uint8_t> src(2 * 2 * 4, 0xAB);
  std::vector<std::uint8_t> out(2 * 2 * 4, 0x11);
  stencil_cli_cropImageRGBA(src.data(), 2, 2, 1, 1, 2, 2, out.data());
  CHECK(out[0] == 0xAB);                      // the one overlapping pixel
  CHECK((out[4] == 0 && out[8] == 0 && out[12] == 0));  // the three outside it

  // A NULL / empty point list is a no-op rasterise, not a crash.
  std::vector<std::uint8_t> buf(4 * 4 * 4, 9);
  const std::vector<std::uint8_t> before = buf;
  stencil_cli_rasterizeLine(buf.data(), 4, 4, nullptr, 0, "red", 1, 1, "solid", 0, "", "");
  CHECK(buf == before);
}
