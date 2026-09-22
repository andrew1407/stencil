// The context-taking formula exports of abi/shared.inc: one body, two extern "C" spellings.
// Split from abiShared.test.cpp, which pins the rest of that file.
#include "doctest.h"
#include "cliApi.h"

#include <limits>

extern "C" {
  int stencil_formulaValidateCtx(const char*, double, double, double, double, double, double,
                                 const char*);
  double stencil_formulaApplyCtx(const char*, int, double, int, double, double, double, double,
                                 double, double, const char*);
}

TEST_CASE("shared ABI: the context forms agree, and NaN means 'not supplied'") {
  const int x = static_cast<int>('x');
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (const char* expr : {"PAGE_WIDTH", "PAGE_HEIGHT_IN", "IMAGE_WIDTH / 2", "x / y",
                           "PAGE_WIDTH + PAGE_HEIGHT - x / 2", "PAGE_WIDTHS"}) {
    CHECK(stencil_formulaValidateCtx(expr, 10, 4, 21, 29.7, 600, 400, "cm") ==
          stencil_cli_validateFormulaCtx(expr, 10, 4, 21, 29.7, 600, 400, "cm"));
    const double w = stencil_formulaApplyCtx(expr, x, 8.0, 1, 10, 4, 21, 29.7, 600, 400, "cm");
    const double c =
        stencil_cli_applyFormulaCtx(expr, x, 8.0, 1, 10, 4, 21, 29.7, 600, 400, "cm");
    CHECK(((w == c) || (w != w && c != c)));
  }
  // "in" is the only unit that converts; a NaN field leaves its name unknown (-> identity).
  CHECK(stencil_formulaApplyCtx("PAGE_WIDTH", x, 0.0, 1, nan, nan, 21, 29.7, nan, nan, "in") ==
        doctest::Approx(21.0 / 2.54));
  CHECK(stencil_formulaValidateCtx("IMAGE_WIDTH", nan, nan, 21, 29.7, nan, nan, "cm") == 0);
  CHECK(stencil_formulaApplyCtx("IMAGE_WIDTH", x, 7.0, 1, nan, nan, 21, 29.7, nan, nan, "cm") ==
        doctest::Approx(7.0));
  // An unbound axis validates at 1, and a NULL unit reads as cm.
  CHECK(stencil_formulaValidateCtx("x / y", nan, nan, 21, 29.7, 600, 400, nullptr) == 1);
  CHECK(stencil_cli_validateFormulaCtx(nullptr, nan, nan, nan, nan, nan, nan, nullptr) == 1);
}
