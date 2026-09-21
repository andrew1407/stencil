// The exports whose bodies live once in abi/shared.inc and are emitted into both
// extern "C" ABIs. Each case calls the wasm spelling and the CLI spelling and
// asserts they agree, so a future edit to one surface cannot silently fork.
#include "doctest.h"
#include "cliApi.h"

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
  const char* stencil_pageFormats(void);
  int stencil_formulaValidate(const char*, int);
  double stencil_formulaApply(const char*, int, double, int);
  void stencil_applyContourRGBA(std::uint8_t*, int, int);
  int stencil_parseDuration(const char*, long long*);
}

TEST_CASE("shared ABI: pageFormats is one list on both surfaces") {
  CHECK(std::string(stencil_pageFormats()) == std::string(stencil_cli_pageFormats()));
  CHECK(std::string(stencil_pageFormats()).find("A4") != std::string::npos);
}

TEST_CASE("shared ABI: formula validate/apply agree on both surfaces") {
  const int x = static_cast<int>('x');
  for (const char* expr : {"x + 1", "x +", "", "x ** 2", "x / 0"}) {
    CHECK(stencil_formulaValidate(expr, x) == stencil_cli_validateFormula(expr, x));
    const double w = stencil_formulaApply(expr, x, 3.0, 1);
    const double c = stencil_cli_applyFormula(expr, x, 3.0, 1);
    CHECK(((w == c) || (w != w && c != c)));  // NaN == NaN is false; treat both-NaN as agreeing
  }
  // A null expr is the empty (identity) expression, not a crash, on both.
  CHECK(stencil_formulaValidate(nullptr, x) == 1);
  CHECK(stencil_cli_validateFormula(nullptr, x) == 1);
  CHECK(stencil_formulaApply(nullptr, x, 7.0, 1) == doctest::Approx(7.0));
  CHECK(stencil_cli_applyFormula(nullptr, x, 7.0, 1) == doctest::Approx(7.0));
}

TEST_CASE("shared ABI: applyContour writes the same pixels on both surfaces") {
  std::vector<std::uint8_t> a(4 * 4 * 4, 0), b;
  for (std::size_t i = 0; i < a.size(); i += 4) {
    a[i] = static_cast<std::uint8_t>(i);
    a[i + 1] = 40;
    a[i + 2] = 200;
    a[i + 3] = 255;
  }
  b = a;
  stencil_applyContourRGBA(a.data(), 4, 4);
  stencil_cli_applyContour(b.data(), 4, 4);
  CHECK(a == b);
}

TEST_CASE("shared ABI: parseDuration returns int64 ms on both surfaces") {
  struct Case { const char* spec; bool ok; long long ms; };
  const long long day = 24LL * 60 * 60 * 1000;
  const Case cases[] = {
      {"days 23", true, 23 * day},
      {"fortnight", true, 14 * day},
      {"month", true, 30 * day},
      {"3 weeks", true, 21 * day},
      {"off", true, 0},
      {"banana", false, 0},
      {"days 0", false, 0},
      // The parser's ceiling is Number.isSafeInteger (2^53-1), so the widest
      // accepted value still round-trips exactly through a JS number.
      {"days 100000000", true, 100000000LL * day},
      {"days 200000000", false, 0},
  };
  for (const Case& c : cases) {
    long long w = -1, cli = -1;
    CHECK(stencil_parseDuration(c.spec, &w) == (c.ok ? 1 : 0));
    CHECK(stencil_cli_parseDuration(c.spec, &cli) == (c.ok ? 1 : 0));
    if (!c.ok) continue;
    CHECK(w == c.ms);
    CHECK(cli == c.ms);
    CHECK(w <= 9007199254740991LL);
  }
  // A null out pointer is tolerated by both (the caller only wanted validity).
  CHECK(stencil_parseDuration("week", nullptr) == 1);
  CHECK(stencil_cli_parseDuration("week", nullptr) == 1);
}
