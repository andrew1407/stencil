#pragma once
#include <string>

// A small, safe human-duration parser — the shared C++ engine behind the
// `expire` command (the CLI console verb and the browser's `stencil.expire`).
// The JS twin kept behaviorally identical is browser/js/core/durationParser.js.
//
// It turns a free-form spec into a length in milliseconds; the caller adds that
// to "now" to get an expiry timestamp (0 == "keep forever"). Parsing is pure and
// clock-free — core never reads the clock (STL-only, GUI-free).
//
// Grammar (whitespace-tokenized, case-insensitive; 1 or 2 tokens):
//
//   spec  := off | unit | count unit | unit count
//   off   := 'off' | 'never' | 'none'                     -> 0 ("keep forever")
//   unit  := 'day' | 'week' | 'fortnight' | 'month' | 'year'  (trailing 's' ok)
//   count := a positive base-10 integer
//
// A bare unit means one of it ("day" = 1 day, "month" = 1 month, "fortnight" =
// 14 days). Fixed durations — week=7d, fortnight=14d, month=30d, year=365d — so
// this C++ port and the JS twin agree with no calendar library (mirrors the
// PERIOD_MS presets in projectsStore). Empty / unknown unit / non-positive or
// non-integer count / overflow are all reported invalid, so a bare `/expire`
// prints help instead of applying anything.
namespace stencil::core {

  class DurationParser {
   public:
    // Milliseconds per day — the base unit all others multiply. Public so the
    // adapters can mirror the same constants without re-deriving them.
    static constexpr long long DAY_MS = 24LL * 60 * 60 * 1000;

    // Parse `spec` into a duration in ms written to `outMs` (0 for off/never).
    // Returns true iff the spec is valid; leaves `outMs` untouched on failure.
    bool parse(const std::string& spec, long long& outMs) const;
  };

}
