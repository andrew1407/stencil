#pragma once
#include <string>

// Human-duration parser behind the `expire` command; the twin kept identical is
// browser/js/core/durationParser.js. Clock-free: the caller adds the ms to "now".
// Grammar (whitespace-tokenized, case-insensitive; 1 or 2 tokens):
//
//   spec  := off | unit | count unit | unit count
//   off   := 'off' | 'never' | 'none'                     -> 0 ("keep forever")
//   unit  := 'day' | 'week' | 'fortnight' | 'month' | 'year'  (trailing 's' ok)
//   count := a positive base-10 integer
//
// A bare unit means one of it. Fixed durations (month=30d, year=365d) so the two
// ports agree with no calendar library — the PERIOD_MS presets of projectsStore.
namespace stencil::core {

  class DurationParser {
   public:
    static constexpr long long DAY_MS = 24LL * 60 * 60 * 1000;

    // The vocabulary, space-separated in help order; adapters print their `expire`
    // help from these so a printed list cannot drift from what parse() accepts.
    static const char* unitNames();
    static const char* offAliases();

    // `outMs` is untouched on failure.
    bool parse(const std::string& spec, long long& outMs) const;
  };

}
