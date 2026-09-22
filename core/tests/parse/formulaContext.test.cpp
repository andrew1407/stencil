#include "doctest.h"
#include "formulaParser.hpp"
#include <string>
#include <vector>

using namespace stencil::core;

// Mirrors browser/tests/core/parse/formulaContext.test.js.

namespace {

  // A4 portrait behind a 600x400 image, with the other axis already resolved.
  FormulaContext a4(const std::string& unit = "cm") {
    FormulaContext c;
    c.x = 10.0;
    c.y = 4.0;
    c.pageWidthCm = 21.0;
    c.pageHeightCm = 29.7;
    c.imageWidth = 600.0;
    c.imageHeight = 400.0;
    c.unit = unit;
    return c;
  }

  struct Case {
    const char* expr;
    char var;      // the axis `value` binds, exactly as pixelToPageCoords calls it
    double value;
    double expected;
  };

}  // namespace

TEST_CASE("the named constants, both axes and a bare number all evaluate") {
  const std::vector<Case> cases = {
      {"PAGE_WIDTH", 'x', 0.0, 21.0},
      {"PAGE_HEIGHT", 'x', 0.0, 29.7},
      {"PAGE_WIDTH_CM", 'x', 0.0, 21.0},
      {"PAGE_HEIGHT_CM", 'x', 0.0, 29.7},
      {"PAGE_WIDTH_IN", 'x', 0.0, 21.0 / 2.54},
      {"PAGE_HEIGHT_IN", 'x', 0.0, 29.7 / 2.54},
      {"IMAGE_WIDTH", 'x', 0.0, 600.0},
      {"IMAGE_HEIGHT", 'y', 0.0, 400.0},
      {"9", 'x', 5.0, 9.0},                      // no variable at all
      {"IMAGE_HEIGHT", 'x', 5.0, 400.0},         // a constant alone
      {"x / y", 'x', 10.0, 2.5},                 // f(x) reads y
      {"x - 3", 'y', 99.0, 7.0},                 // f(y) reads x
      {"PAGE_WIDTH + PAGE_HEIGHT - x / 2", 'x', 8.0, 46.7},
      {"IMAGE_WIDTH / PAGE_WIDTH", 'x', 0.0, 600.0 / 21.0},
  };
  for (const Case& c : cases) {
    CHECK_MESSAGE(FormulaParser::apply(c.expr, c.var, c.value, true, a4()) ==
                      doctest::Approx(c.expected),
                  c.expr);
    CHECK_MESSAGE(FormulaParser::validate(c.expr, a4()), c.expr);
  }
}

TEST_CASE("PAGE_WIDTH / PAGE_HEIGHT follow the selected unit; the suffixed forms do not") {
  CHECK(*FormulaParser::evaluate("PAGE_WIDTH", a4("cm")) == doctest::Approx(21.0));
  CHECK(*FormulaParser::evaluate("PAGE_WIDTH", a4("in")) == doctest::Approx(21.0 / 2.54));
  CHECK(*FormulaParser::evaluate("PAGE_HEIGHT", a4("in")) == doctest::Approx(29.7 / 2.54));
  CHECK(*FormulaParser::evaluate("PAGE_HEIGHT_CM", a4("in")) == doctest::Approx(29.7));
  CHECK(*FormulaParser::evaluate("PAGE_WIDTH_IN", a4("cm")) == doctest::Approx(21.0 / 2.54));
}

TEST_CASE("names are case-sensitive and matched whole") {
  for (const char* expr : {"PAGE_WIDTHS", "page_width", "Page_Width", "PAGE", "X", "Y",
                           "PAGE_WIDTH_MM", "IMAGE_WIDTH2", "foo"}) {
    CHECK_FALSE_MESSAGE(FormulaParser::validate(expr, a4()), expr);
    CHECK_MESSAGE(FormulaParser::apply(expr, 'x', 42.0, true, a4()) == doctest::Approx(42.0),
                  expr);
  }
}

TEST_CASE("a constant the caller did not supply is invalid, never zero") {
  FormulaContext blank;  // nothing open: no page, no image
  blank.x = 3.0;
  CHECK_FALSE(FormulaParser::validate("IMAGE_WIDTH", blank));
  CHECK_FALSE(FormulaParser::validate("PAGE_WIDTH", blank));
  CHECK(FormulaParser::apply("IMAGE_WIDTH * 2", 'x', 7.0, true, blank) == doctest::Approx(7.0));
  CHECK(FormulaParser::apply("x * 2", 'x', 7.0, true, blank) == doctest::Approx(14.0));
}

TEST_CASE("validate probes an unbound axis at 1, so a cross-axis formula passes") {
  FormulaContext ctx = a4();
  ctx.x = kFormulaUnset;
  ctx.y = kFormulaUnset;
  CHECK(FormulaParser::validate("x / y", ctx));
  CHECK(FormulaParser::validate("x - 3", ctx));
  CHECK(FormulaParser::validate("", ctx));
  CHECK(FormulaParser::validate("   ", ctx));
  CHECK(FormulaParser::apply("  ", 'x', 5.0, true, ctx) == doctest::Approx(5.0));
  CHECK(FormulaParser::apply("PAGE_WIDTH", 'x', 5.0, false, ctx) == doctest::Approx(5.0));
}

TEST_CASE("division by zero and the depth cap are unchanged by a context") {
  FormulaContext ctx = a4();
  ctx.y = 0.0;
  CHECK_FALSE(FormulaParser::evaluate("x / y", ctx).has_value());
  CHECK(FormulaParser::apply("x / y", 'x', 5.0, true, ctx) == doctest::Approx(5.0));
  const std::string balanced =
      std::string(5000, '(') + "PAGE_WIDTH" + std::string(5000, ')');
  CHECK_FALSE(FormulaParser::validate(balanced, a4()));
  CHECK(FormulaParser::apply(balanced, 'x', 42.0, true, a4()) == doctest::Approx(42.0));
}

TEST_CASE("the context-free entry points behave exactly as before") {
  CHECK_FALSE(FormulaParser::validate("PAGE_WIDTH", 'x'));
  CHECK(FormulaParser::apply("PAGE_WIDTH", 'x', 5.0, true) == doctest::Approx(5.0));
  CHECK_FALSE(FormulaParser::evaluate("y", 'x', 1.0).has_value());
  CHECK(FormulaParser::apply("x * 2", 'x', 5.0, true) == doctest::Approx(10.0));
}
