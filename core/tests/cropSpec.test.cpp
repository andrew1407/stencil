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
