#include "doctest.h"
#include "core/formulaParser.hpp"

using namespace stencil::core;

static const FormulaParser fp;

// Mirrors browser/tests/formula.test.js.

TEST_CASE("validate empty / whitespace = true (identity)") {
  CHECK(fp.validate("", 'x'));
  CHECK(fp.validate("  ", 'x'));
}

TEST_CASE("validate valid expression true") {
  CHECK(fp.validate("x*2", 'x'));
}

TEST_CASE("validate syntax error false") {
  CHECK_FALSE(fp.validate("x+", 'x'));
}

TEST_CASE("validate unknown identifier (function) false") {
  CHECK_FALSE(fp.validate("foo(x)", 'x'));
}

TEST_CASE("validate non-finite (1/0) false") {
  CHECK_FALSE(fp.validate("1/0", 'x'));
}

TEST_CASE("apply with allowFormulas true") {
  CHECK(fp.apply("x*2", 'x', 5, true) == doctest::Approx(10.0));
}

TEST_CASE("apply with allowFormulas false -> identity") {
  CHECK(fp.apply("x*2", 'x', 5, false) == doctest::Approx(5.0));
}

TEST_CASE("apply with empty expr -> identity") {
  CHECK(fp.apply("", 'x', 5, true) == doctest::Approx(5.0));
}

TEST_CASE("apply with invalid expr -> original value") {
  CHECK(fp.apply("x+", 'x', 5, true) == doctest::Approx(5.0));
}

// Additional coverage for the recursive-descent grammar (new vs. the JS eval).

TEST_CASE("operator precedence: + and *") {
  CHECK(*fp.evaluate("2+3*4", 'x', 0) == doctest::Approx(14.0));
}

TEST_CASE("parentheses override precedence") {
  CHECK(*fp.evaluate("(2+3)*4", 'x', 0) == doctest::Approx(20.0));
}

TEST_CASE("power is right-associative") {
  CHECK(*fp.evaluate("2**3**2", 'x', 0) == doctest::Approx(512.0));  // 2^(3^2)
}

TEST_CASE("power binds tighter than multiply") {
  CHECK(*fp.evaluate("2*3**2", 'x', 0) == doctest::Approx(18.0));
}

TEST_CASE("unary minus and exponent of negative") {
  CHECK(*fp.evaluate("-2**2", 'x', 0) == doctest::Approx(-4.0));   // -(2^2)
  CHECK(*fp.evaluate("2**-2", 'x', 0) == doctest::Approx(0.25));
}

TEST_CASE("variable substitution with y") {
  CHECK(fp.apply("y/2+1", 'y', 10, true) == doctest::Approx(6.0));
}

TEST_CASE("decimal and whitespace tolerant") {
  CHECK(*fp.evaluate("  1.5 *  x ", 'x', 4) == doctest::Approx(6.0));
}

TEST_CASE("rejects trailing operator and unbalanced parens") {
  CHECK_FALSE(fp.evaluate("2*", 'x', 0).has_value());
  CHECK_FALSE(fp.evaluate("(2+3", 'x', 0).has_value());
  CHECK_FALSE(fp.evaluate("2)", 'x', 0).has_value());
}

TEST_CASE("wrong variable name is rejected") {
  CHECK_FALSE(fp.evaluate("y", 'x', 1).has_value());
}

// S11 parity: the browser examples used in the page-coord composition
// (drawingApp.js validateAndApplyFormulas / pixelToPageCoords).
TEST_CASE("S11 parity: x+9 shifts x by 9") {
  CHECK(fp.apply("x + 9", 'x', 3.0, true) == doctest::Approx(12.0));
}

TEST_CASE("S11 parity: (y-7)*4") {
  CHECK(fp.apply("(y-7)*4", 'y', 10.0, true) == doctest::Approx(12.0));
}

TEST_CASE("S11 parity: disabling formulas restores the raw cm value") {
  CHECK(fp.apply("x + 9", 'x', 3.0, false) == doctest::Approx(3.0));
}
