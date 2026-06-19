#include "doctest.h"
#include "cliApi.h"

#include <cstdint>
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
  CHECK(stencil_cli_namedPageSize("nope", &wcm, &hcm) == 0);

  int pw = 0, ph = 0;
  stencil_cli_defaultBlankSizePx(21.0, 29.7, 96.0, &pw, &ph);
  CHECK(pw > 0);
  CHECK(ph > pw);  // portrait
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

TEST_CASE("stencil_cli_rasterizeLine draws into the buffer") {
  const int w = 16, h = 16;
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 4, 0);
  const double pts[] = {2, 8, 14, 8};
  stencil_cli_rasterizeLine(buf.data(), w, h, pts, 2, "red", 3, 0, "solid", 0,
                            "transparent");
  CHECK(buf[(8 * w + 8) * 4 + 0] > 100);  // red along the stroke
}
