#include "doctest.h"
#include "colorNames.hpp"

using namespace stencil::core;

TEST_CASE("parseColor: CSS named colours, case-insensitive") {
  auto red = parseColor("red");
  REQUIRE(red);
  CHECK(red->r == 255);
  CHECK(red->g == 0);
  CHECK(red->b == 0);
  CHECK(red->a == 255);

  auto rp = parseColor("REBECCAPURPLE");
  REQUIRE(rp);
  CHECK(rp->r == 0x66);
  CHECK(rp->g == 0x33);
  CHECK(rp->b == 0x99);

  CHECK_FALSE(parseColor("notacolour"));
  CHECK_FALSE(parseColor(""));
}

TEST_CASE("parseColor: hex forms") {
  auto s = parseColor("#fff");          // short -> expanded
  REQUIRE(s);
  CHECK(s->r == 255);
  CHECK(s->g == 255);
  CHECK(s->b == 255);

  auto l = parseColor("#1a2b3c");
  REQUIRE(l);
  CHECK(l->r == 0x1a);
  CHECK(l->g == 0x2b);
  CHECK(l->b == 0x3c);
  CHECK(l->a == 255);

  auto rgba = parseColor("#11223344");
  REQUIRE(rgba);
  CHECK(rgba->a == 0x44);

  CHECK_FALSE(parseColor("#12"));       // wrong length
  CHECK_FALSE(parseColor("#zzzzzz"));   // non-hex
}

TEST_CASE("parseColor: transparent keyword") {
  auto t = parseColor("transparent");
  REQUIRE(t);
  CHECK(t->a == 0);
}
