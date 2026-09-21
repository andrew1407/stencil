#include "doctest.h"
#include "cropSpec.hpp"

using namespace stencil::core;

TEST_CASE("parseCropSpec: the documented string form") {
  auto cs = parseCropSpec("x1 = 90 x2 = 200, y1 = 90 y2 = 567");
  CHECK(cs.valid);
  REQUIRE(cs.x1);
  CHECK(*cs.x1 == "90");
  CHECK(*cs.x2 == "200");
  CHECK(*cs.y1 == "90");
  CHECK(*cs.y2 == "567");
}

TEST_CASE("parseCropSpec: spacing variants and partial specs") {
  auto cs = parseCropSpec("x1=10%  y2= 20px");
  CHECK(cs.valid);
  REQUIRE(cs.x1);
  CHECK(*cs.x1 == "10%");
  CHECK_FALSE(cs.x2);
  REQUIRE(cs.y2);
  CHECK(*cs.y2 == "20px");
}

TEST_CASE("parseCropSpec: unknown keys invalidate") {
  CHECK_FALSE(parseCropSpec("z1 = 5").valid);
  CHECK_FALSE(parseCropSpec("x1 = ").valid);
}

TEST_CASE("resolveCropRect: explicit pixel edges") {
  CropResolveParams p;
  p.imageW = 200; p.imageH = 200;
  p.pxPerCmX = 10; p.pxPerCmY = 10;
  p.pageWidth = 21; p.pageHeight = 29.7;

  auto cs = parseCropSpec("x1 = 0px x2 = 100px y1 = 0px y2 = 50px");
  auto r = resolveCropRect(cs, p, /*album=*/false);
  REQUIRE(r);
  CHECK(r->x == doctest::Approx(0.0));
  CHECK(r->y == doctest::Approx(0.0));
  CHECK(r->width == doctest::Approx(100.0));
  CHECK(r->height == doctest::Approx(50.0));
}

TEST_CASE("resolveCropRect: a single axis derives the other from the page proportion") {
  CropResolveParams p;
  p.imageW = 1000; p.imageH = 1000;
  p.pxPerCmX = 10; p.pxPerCmY = 10;
  p.pageWidth = 21; p.pageHeight = 29.7;  // A4

  // Only X given (width 100px). Portrait aspect = 21/29.7, so derived height is
  // width / aspect = 100 * 29.7/21 ≈ 141.4.
  auto cs = parseCropSpec("x1 = 0px x2 = 100px");
  auto r = resolveCropRect(cs, p, /*album=*/false);
  REQUIRE(r);
  CHECK(r->width == doctest::Approx(100.0));
  CHECK(r->height == doctest::Approx(100.0 * 29.7 / 21.0));
}

TEST_CASE("resolveCropRect: empty spec is the whole image; bad token fails") {
  CropResolveParams p;
  p.imageW = 640; p.imageH = 480;
  p.pxPerCmX = 10; p.pxPerCmY = 10;
  p.pageWidth = 21; p.pageHeight = 29.7;

  auto whole = resolveCropRect(parseCropSpec(""), p, false);
  REQUIRE(whole);
  CHECK(whole->width == doctest::Approx(640.0));
  CHECK(whole->height == doctest::Approx(480.0));

  CHECK_FALSE(resolveCropRect(parseCropSpec("x1 = bogus"), p, false));
}

TEST_CASE("parseCropSpec: the aspect key is accepted") {
  auto cs = parseCropSpec("x1 = 10px, aspect = 4:3");
  CHECK(cs.valid);
  REQUIRE(cs.aspect);
  CHECK(*cs.aspect == "4:3");
  CHECK(*cs.x1 == "10px");
}

static stencil::core::CropResolveParams aspectParams(double w, double h) {
  stencil::core::CropResolveParams p;
  p.imageW = w; p.imageH = h;
  p.pxPerCmX = 10; p.pxPerCmY = 10;
  p.pageWidth = 21; p.pageHeight = 29.7;
  return p;
}

TEST_CASE("resolveCropRect: aspect shrinks the width about the centre") {
  // Edge rect 200x100 is too wide for 1:1 -> width shrinks to 100, centred at x=100.
  auto cs = parseCropSpec("x1 = 0px x2 = 200px y1 = 0px y2 = 100px aspect = 1:1");
  auto r = resolveCropRect(cs, aspectParams(400, 400), false);
  REQUIRE(r);
  CHECK(r->x == doctest::Approx(50.0));
  CHECK(r->y == doctest::Approx(0.0));
  CHECK(r->width == doctest::Approx(100.0));
  CHECK(r->height == doctest::Approx(100.0));
}

TEST_CASE("resolveCropRect: aspect shrinks the height about the centre") {
  // The same 200x100 rect is too tall for 4:1 -> height shrinks to 50, centred at y=50.
  auto cs = parseCropSpec("x1 = 0px x2 = 200px y1 = 0px y2 = 100px aspect = 4:1");
  auto r = resolveCropRect(cs, aspectParams(400, 400), false);
  REQUIRE(r);
  CHECK(r->x == doctest::Approx(0.0));
  CHECK(r->y == doctest::Approx(25.0));
  CHECK(r->width == doctest::Approx(200.0));
  CHECK(r->height == doctest::Approx(50.0));
}

TEST_CASE("resolveCropRect: an exact-fit aspect is a no-op") {
  auto cs = parseCropSpec("x1 = 10px x2 = 210px y1 = 20px y2 = 120px aspect = 2:1");
  auto r = resolveCropRect(cs, aspectParams(400, 400), false);
  REQUIRE(r);
  CHECK(r->x == doctest::Approx(10.0));
  CHECK(r->y == doctest::Approx(20.0));
  CHECK(r->width == doctest::Approx(200.0));
  CHECK(r->height == doctest::Approx(100.0));
}

TEST_CASE("resolveCropRect: aspect alone applies to the full image") {
  auto r = resolveCropRect(parseCropSpec("aspect = 1:1"), aspectParams(640, 480), false);
  REQUIRE(r);
  CHECK(r->x == doctest::Approx(80.0));
  CHECK(r->y == doctest::Approx(0.0));
  CHECK(r->width == doctest::Approx(480.0));
  CHECK(r->height == doctest::Approx(480.0));

  // Already at the ratio -> the whole image, untouched.
  auto same = resolveCropRect(parseCropSpec("aspect = 4:3"), aspectParams(640, 480), false);
  REQUIRE(same);
  CHECK(same->x == doctest::Approx(0.0));
  CHECK(same->width == doctest::Approx(640.0));
  CHECK(same->height == doctest::Approx(480.0));
}

TEST_CASE("resolveCropRect: aspect keeps fractional centres exactly (no early rounding)") {
  // 101x100 rect, 1:1 -> width 100, so x moves by half a pixel: 0.5. Rounding stays the
  // caller's job (stencil_cli_resolveCrop lrounds), exactly as for the plain edge path.
  auto cs = parseCropSpec("x1 = 0px x2 = 101px y1 = 0px y2 = 100px aspect = 1:1");
  auto r = resolveCropRect(cs, aspectParams(400, 400), false);
  REQUIRE(r);
  CHECK(r->x == doctest::Approx(0.5));
  CHECK(r->width == doctest::Approx(100.0));
  CHECK(r->height == doctest::Approx(100.0));
}

TEST_CASE("resolveCropRect: a degenerate aspect result clamps to 1px, centre kept") {
  auto cs = parseCropSpec("x1 = 0px x2 = 100px y1 = 0px y2 = 100px aspect = 1000:1");
  auto r = resolveCropRect(cs, aspectParams(400, 400), false);
  REQUIRE(r);
  CHECK(r->width == doctest::Approx(100.0));   // never grows past the resolved rect
  CHECK(r->height == doctest::Approx(1.0));    // 0.1px floored to 1px
  CHECK(r->y == doctest::Approx(49.5));        // centre preserved: 49.5 + 0.5 = 50
  CHECK(r->x == doctest::Approx(0.0));
}

TEST_CASE("resolveCropRect: invalid aspect strings fail like invalid tokens") {
  const auto p = aspectParams(640, 480);
  for (const char* bad : {"0:3", "4:0", "-4:3", "4:-3", "4", "4:", ":3", "4:3:2",
                          "a:b", "4.5:3", "1e2:3"}) {
    auto cs = parseCropSpec(std::string("aspect = ") + bad);
    CHECK(cs.valid);                                  // parse keeps the token as-is…
    CHECK_FALSE(resolveCropRect(cs, p, false));       // …resolution rejects it
  }
  // A missing value is a structural error, same as "x1 = ".
  CHECK_FALSE(parseCropSpec("aspect = ").valid);
}
