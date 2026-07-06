#include "doctest.h"
#include "durationParser.hpp"

using namespace stencil::core;

static const DurationParser dp;

// Mirrors browser/tests/durationParser.test.js.

static constexpr long long DAY = DurationParser::DAY_MS;

static long long parseOK(const std::string& spec) {
  long long ms = -1;
  REQUIRE(dp.parse(spec, ms));
  return ms;
}

TEST_CASE("bare units mean one of them") {
  CHECK(parseOK("day") == DAY);
  CHECK(parseOK("week") == 7 * DAY);
  CHECK(parseOK("fortnight") == 14 * DAY);
  CHECK(parseOK("month") == 30 * DAY);
  CHECK(parseOK("year") == 365 * DAY);
}

TEST_CASE("count then unit") {
  CHECK(parseOK("days 1") == DAY);
  CHECK(parseOK("days 23") == 23 * DAY);
  CHECK(parseOK("months 3") == 3 * 30 * DAY);
}

TEST_CASE("unit then count (either order)") {
  CHECK(parseOK("3 months") == 3 * 30 * DAY);
  CHECK(parseOK("2 fortnights") == 28 * DAY);
}

TEST_CASE("plural and singular both accepted") {
  CHECK(parseOK("day 2") == 2 * DAY);
  CHECK(parseOK("days 2") == 2 * DAY);
}

TEST_CASE("case-insensitive and whitespace tolerant") {
  CHECK(parseOK("  MONTHS   3 ") == 3 * 30 * DAY);
  CHECK(parseOK("Fortnight") == 14 * DAY);
}

TEST_CASE("off / never / none = keep forever (0)") {
  CHECK(parseOK("off") == 0);
  CHECK(parseOK("never") == 0);
  CHECK(parseOK("none") == 0);
}

TEST_CASE("invalid specs are rejected") {
  long long ms = 0;
  CHECK_FALSE(dp.parse("", ms));
  CHECK_FALSE(dp.parse("   ", ms));
  CHECK_FALSE(dp.parse("banana", ms));
  CHECK_FALSE(dp.parse("days 0", ms));       // non-positive count
  CHECK_FALSE(dp.parse("days -3", ms));      // sign is not a digit
  CHECK_FALSE(dp.parse("days 2.5", ms));     // non-integer
  CHECK_FALSE(dp.parse("3 days 2", ms));     // too many tokens
  CHECK_FALSE(dp.parse("3 weeks banana", ms));
}

TEST_CASE("bare plural unit is valid (= one)") {
  CHECK(parseOK("days") == DAY);
}

TEST_CASE("overflow is rejected") {
  long long ms = 0;
  CHECK_FALSE(dp.parse("days 99999999999999999999", ms));  // count overflow
  CHECK_FALSE(dp.parse("years 100000000000000000", ms));   // product overflow
}

TEST_CASE("product cap tracks JS Number.MAX_SAFE_INTEGER (parity)") {
  // 1e8 days fits in 2^53 - 1; 2e8 days overflows it. The bound is the JS
  // isSafeInteger check, not int64, so the wasm value round-trips exactly.
  CHECK(parseOK("days 100000000") == 100000000LL * DAY);
  long long ms = 0;
  CHECK_FALSE(dp.parse("days 200000000", ms));  // product > 2^53 - 1
}

TEST_CASE("outMs untouched on failure") {
  long long ms = 12345;
  CHECK_FALSE(dp.parse("nope", ms));
  CHECK(ms == 12345);
}
