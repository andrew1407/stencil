#pragma once
#include <string>

// Human-duration parser behind `expire`; twin browser/js/core/durationParser.js. Clock-free.
//   spec  := off | unit | count unit | unit count
//   off   := 'off' | 'never' | 'none' -> 0 ("keep forever")
//   unit  := day | week | fortnight | month | year (trailing 's' ok); a bare unit means one
// Fixed durations (month=30d, year=365d) so the ports agree - projectsStore's PERIOD_MS.
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
