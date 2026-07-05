#include "doctest.h"
#include "formulaParser.hpp"

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

// Security/robustness: untrusted input must never crash or hang the parser.
TEST_CASE("deeply nested parens are invalid (identity), not a stack overflow") {
  // Far past the recursion cap: this used to overflow the stack; now it's invalid.
  const std::string deep(200000, '(');
  CHECK_FALSE(fp.validate(deep, 'x'));
  CHECK_FALSE(fp.evaluate(deep, 'x', 1).has_value());
  // A balanced but very deeply nested expression is likewise rejected as invalid,
  // and apply() falls back to the identity value rather than misbehaving.
  const std::string balanced = std::string(5000, '(') + "x" + std::string(5000, ')');
  CHECK_FALSE(fp.validate(balanced, 'x'));
  CHECK(fp.apply(balanced, 'x', 42.0, true) == doctest::Approx(42.0));
  // A long unary-sign chain recurses through parseUnary — also capped.
  CHECK_FALSE(fp.validate(std::string(200000, '-') + "x", 'x'));
}

TEST_CASE("a long flat expression stays linear and valid") {
  // No nesting -> handled by the iterative +/* loops, not recursion. Must succeed.
  std::string flat = "0";
  for (int i = 0; i < 20000; ++i) flat += "+1";
  const auto v = fp.evaluate(flat, 'x', 0.0);
  REQUIRE(v.has_value());
  CHECK(*v == doctest::Approx(20000.0));
}

TEST_CASE("numeric overflow yields invalid (identity), matching the finite contract") {
  CHECK_FALSE(fp.evaluate("9e999", 'x', 0).has_value());        // std::stod out_of_range
  CHECK_FALSE(fp.evaluate("1e308*1e308", 'x', 0).has_value());  // -> +inf
  CHECK_FALSE(fp.evaluate("2**2**2**2**2", 'x', 0).has_value()); // 2^65536 -> inf
  CHECK(fp.apply("1e308*1e308", 'x', 7.0, true) == doctest::Approx(7.0));
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
