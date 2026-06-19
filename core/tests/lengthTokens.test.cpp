#include "doctest.h"
#include "lengthTokens.hpp"

using namespace stencil::core;

TEST_CASE("parseLengthToken: bare numbers are signed deltas") {
  auto a = parseLengthToken("10");
  REQUIRE(a);
  CHECK(a->kind == LengthKind::Delta);
  CHECK(a->value == doctest::Approx(10.0));

  auto b = parseLengthToken("-10");
  REQUIRE(b);
  CHECK(b->kind == LengthKind::Delta);
  CHECK(b->value == doctest::Approx(-10.0));  // sign folded into the delta

  auto c = parseLengthToken(".5");
  REQUIRE(c);
  CHECK(c->value == doctest::Approx(0.5));
}

TEST_CASE("parseLengthToken: units and the from-end '-' flag") {
  auto px = parseLengthToken("120px");
  REQUIRE(px);
  CHECK(px->kind == LengthKind::Px);
  CHECK(px->value == doctest::Approx(120.0));
  CHECK_FALSE(px->fromEnd);

  auto pct = parseLengthToken("-60%");
  REQUIRE(pct);
  CHECK(pct->kind == LengthKind::Percent);
  CHECK(pct->value == doctest::Approx(60.0));  // magnitude only
  CHECK(pct->fromEnd);                          // '-' means measured from the end

  CHECK(parseLengthToken("3cm")->value == doctest::Approx(3.0));
  CHECK(parseLengthToken("5mm")->value == doctest::Approx(0.5));   // mm -> cm/10
  CHECK(parseLengthToken("1in")->value == doctest::Approx(2.54));  // in -> cm*2.54
  CHECK(parseLengthToken("1in")->kind == LengthKind::Cm);
}

TEST_CASE("parseLengthToken: rejects malformed input and unknown units") {
  CHECK_FALSE(parseLengthToken(""));
  CHECK_FALSE(parseLengthToken("abc"));
  CHECK_FALSE(parseLengthToken("5."));     // trailing dot
  CHECK_FALSE(parseLengthToken("10ln"));   // not a defined unit
  CHECK_FALSE(parseLengthToken("10 p x"));
}

TEST_CASE("resolveAxisPx: absolute and relative resolution") {
  // 50% of a 200px axis = 100px; from-end mirrors it (still 100 here).
  CHECK(*resolveAxisPx("50%", 200, 10, 0) == doctest::Approx(100.0));
  CHECK(*resolveAxisPx("-25%", 200, 10, 0) == doctest::Approx(150.0));
  // cm uses px-per-cm; px is literal.
  CHECK(*resolveAxisPx("2cm", 200, 10, 0) == doctest::Approx(20.0));
  CHECK(*resolveAxisPx("37px", 200, 10, 0) == doctest::Approx(37.0));
  // A bare number is a delta from currentPx.
  CHECK(*resolveAxisPx("5", 200, 10, 100) == doctest::Approx(105.0));
  CHECK_FALSE(resolveAxisPx("nope", 200, 10, 0));
}
