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
  CHECK(ph > pw);  // portrait
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
                            "transparent");
  CHECK(buf[(8 * w + 8) * 4 + 0] > 100);  // red along the stroke
}
