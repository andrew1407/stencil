#include "doctest.h"
#include "decimal.hpp"
#include "formulaParser.hpp"
#include "lengthTokens.hpp"

#include <clocale>
#include <string>

using namespace stencil::core;


// Mirrors browser/tests/core/parse/formula.test.js.

TEST_CASE("validate empty / whitespace = true (identity)") {
  CHECK(FormulaParser::validate("", 'x'));
  CHECK(FormulaParser::validate("  ", 'x'));
}

TEST_CASE("validate valid expression true") {
  CHECK(FormulaParser::validate("x*2", 'x'));
}

TEST_CASE("validate syntax error false") {
  CHECK_FALSE(FormulaParser::validate("x+", 'x'));
}

TEST_CASE("validate unknown identifier (function) false") {
  CHECK_FALSE(FormulaParser::validate("foo(x)", 'x'));
}

TEST_CASE("validate non-finite (1/0) false") {
  CHECK_FALSE(FormulaParser::validate("1/0", 'x'));
}

TEST_CASE("apply with allowFormulas true") {
  CHECK(FormulaParser::apply("x*2", 'x', 5, true) == doctest::Approx(10.0));
}

TEST_CASE("apply with allowFormulas false -> identity") {
  CHECK(FormulaParser::apply("x*2", 'x', 5, false) == doctest::Approx(5.0));
}

TEST_CASE("apply with empty expr -> identity") {
  CHECK(FormulaParser::apply("", 'x', 5, true) == doctest::Approx(5.0));
}

TEST_CASE("apply with invalid expr -> original value") {
  CHECK(FormulaParser::apply("x+", 'x', 5, true) == doctest::Approx(5.0));
}

// Additional coverage for the recursive-descent grammar (new vs. the JS eval).

TEST_CASE("operator precedence: + and *") {
  CHECK(*FormulaParser::evaluate("2+3*4", 'x', 0) == doctest::Approx(14.0));
}

TEST_CASE("parentheses override precedence") {
  CHECK(*FormulaParser::evaluate("(2+3)*4", 'x', 0) == doctest::Approx(20.0));
}

TEST_CASE("power is right-associative") {
  CHECK(*FormulaParser::evaluate("2**3**2", 'x', 0) == doctest::Approx(512.0));  // 2^(3^2)
}

TEST_CASE("power binds tighter than multiply") {
  CHECK(*FormulaParser::evaluate("2*3**2", 'x', 0) == doctest::Approx(18.0));
}

TEST_CASE("unary minus and exponent of negative") {
  CHECK(*FormulaParser::evaluate("-2**2", 'x', 0) == doctest::Approx(-4.0));   // -(2^2)
  CHECK(*FormulaParser::evaluate("2**-2", 'x', 0) == doctest::Approx(0.25));
}

TEST_CASE("variable substitution with y") {
  CHECK(FormulaParser::apply("y/2+1", 'y', 10, true) == doctest::Approx(6.0));
}

TEST_CASE("decimal and whitespace tolerant") {
  CHECK(*FormulaParser::evaluate("  1.5 *  x ", 'x', 4) == doctest::Approx(6.0));
}

TEST_CASE("rejects trailing operator and unbalanced parens") {
  CHECK_FALSE(FormulaParser::evaluate("2*", 'x', 0).has_value());
  CHECK_FALSE(FormulaParser::evaluate("(2+3", 'x', 0).has_value());
  CHECK_FALSE(FormulaParser::evaluate("2)", 'x', 0).has_value());
}

TEST_CASE("wrong variable name is rejected") {
  CHECK_FALSE(FormulaParser::evaluate("y", 'x', 1).has_value());
}

// Security/robustness: untrusted input must never crash or hang the parser.
TEST_CASE("deeply nested parens are invalid (identity), not a stack overflow") {
  // Far past the recursion cap: this used to overflow the stack; now it's invalid.
  const std::string deep(200000, '(');
  CHECK_FALSE(FormulaParser::validate(deep, 'x'));
  CHECK_FALSE(FormulaParser::evaluate(deep, 'x', 1).has_value());
  // A balanced but very deeply nested expression is likewise rejected as invalid,
  // and apply() falls back to the identity value rather than misbehaving.
  const std::string balanced = std::string(5000, '(') + "x" + std::string(5000, ')');
  CHECK_FALSE(FormulaParser::validate(balanced, 'x'));
  CHECK(FormulaParser::apply(balanced, 'x', 42.0, true) == doctest::Approx(42.0));
  // A long unary-sign chain recurses through parseUnary — also capped.
  CHECK_FALSE(FormulaParser::validate(std::string(200000, '-') + "x", 'x'));
}

TEST_CASE("past the recursion cap is invalid within the length cap too") {
  const std::string over = std::string(300, '(') + "x" + std::string(300, ')');
  REQUIRE(over.size() <= FormulaParser::MAX_CHARS);
  CHECK_FALSE(FormulaParser::validate(over, 'x'));
  CHECK(FormulaParser::validate(std::string(100, '(') + "x" + std::string(100, ')'), 'x'));
}

TEST_CASE("a long flat expression stays linear and valid") {
  // No nesting -> handled by the iterative +/* loops, not recursion. 0+1+1… is 1 + 2n chars.
  std::string flat = "0";
  for (int i = 0; i < 499; ++i) flat += "+1";
  REQUIRE(flat.size() == 999);
  const auto v = FormulaParser::evaluate(flat, 'x', 0.0);
  REQUIRE(v.has_value());
  CHECK(*v == doctest::Approx(499.0));
}

TEST_CASE("an expression past MAX_CHARS is invalid (identity), however simple") {
  CHECK(FormulaParser::MAX_CHARS == 1000);  // LIMITS.formulaMaxChars in common/config/constants.json
  const std::string at = "x" + std::string(FormulaParser::MAX_CHARS - 1, ' ');
  CHECK(FormulaParser::validate(at, 'x'));
  CHECK_FALSE(FormulaParser::validate(at + " ", 'x'));
  CHECK(FormulaParser::apply(at + " ", 'x', 7.0, true) == doctest::Approx(7.0));
  CHECK(FormulaParser::validate(std::string(5000, ' '), 'x'));  // blank: identity, never parsed
}

TEST_CASE("numeric overflow yields invalid (identity), matching the finite contract") {
  CHECK_FALSE(FormulaParser::evaluate("9e999", 'x', 0).has_value());        // a literal of inf
  CHECK_FALSE(FormulaParser::evaluate("1e308*1e308", 'x', 0).has_value());  // -> +inf
  CHECK_FALSE(FormulaParser::evaluate("2**2**2**2**2", 'x', 0).has_value()); // 2^65536 -> inf
  CHECK(FormulaParser::apply("1e308*1e308", 'x', 7.0, true) == doctest::Approx(7.0));
}

// The wasm build has no exceptions: each of these once threw out of std::stod and aborted it.
TEST_CASE("formula: the inputs the browser reads differently parse as its parseFloat does") {
  CHECK_FALSE(FormulaParser::validate(".", 'x'));
  CHECK_FALSE(FormulaParser::validate("x*1e999", 'x'));
  CHECK(FormulaParser::evaluate("1e-400", 'x', 0).value_or(-1.0) == 0.0);  // underflow is 0
  CHECK(FormulaParser::evaluate("1.2.3", 'x', 0).value_or(0.0) == 1.2);   // the longest prefix
  CHECK_FALSE(FormulaParser::validate("1**(1/0)", 'x'));                  // Math.pow(1, inf) is NaN
  CHECK(FormulaParser::evaluate("0.5**(1/0)", 'x', 0).value_or(-1.0) == 0.0);
  CHECK(FormulaParser::evaluate("(0/0)**0", 'x', 0).value_or(-1.0) == 1.0);
  CHECK_FALSE(FormulaParser::validate("\xC2\xA0x", 'x'));  // U+00A0 is not whitespace here
  CHECK_FALSE(FormulaParser::validate("\xC2\xA0", 'x'));
  CHECK(FormulaParser::validate(" \t\v\f\r\n", 'x'));
}

TEST_CASE("formula: a host locale with a ',' point still reads '.'") {
  const std::string saved = std::setlocale(LC_NUMERIC, nullptr);
  if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") || std::setlocale(LC_NUMERIC, "de_DE")) {
    CHECK(FormulaParser::evaluate("x*1.5", 'x', 2.0).value_or(0.0) == doctest::Approx(3.0));
    CHECK(parseDecimal("2.25").value_or(0.0) == 2.25);
    CHECK(parseLengthToken("1.5cm")->value == doctest::Approx(1.5));
  }
  std::setlocale(LC_NUMERIC, saved.c_str());
  CHECK_FALSE(parseDecimal("1,5").has_value());
  CHECK_FALSE(parseDecimal("0x10").has_value());
  CHECK_FALSE(parseDecimal("inf").has_value());
  CHECK_FALSE(parseDecimal(".").has_value());
  std::size_t used = 0;
  CHECK(parseDecimalPrefix("12.5.7", &used).value_or(0.0) == 12.5);
  CHECK(used == 4);
}

// S11 parity: the browser examples used in the page-coord composition
// (drawingApp.js validateAndApplyFormulas / pixelToPageCoords).
TEST_CASE("S11 parity: x+9 shifts x by 9") {
  CHECK(FormulaParser::apply("x + 9", 'x', 3.0, true) == doctest::Approx(12.0));
}

TEST_CASE("S11 parity: (y-7)*4") {
  CHECK(FormulaParser::apply("(y-7)*4", 'y', 10.0, true) == doctest::Approx(12.0));
}

TEST_CASE("S11 parity: disabling formulas restores the raw cm value") {
  CHECK(FormulaParser::apply("x + 9", 'x', 3.0, false) == doctest::Approx(3.0));
}
