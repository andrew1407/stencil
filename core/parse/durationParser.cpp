#include "durationParser.hpp"
#include <cctype>
#include <vector>

namespace stencil::core {

  namespace {

    // Upper bound on a count and on the resulting ms — JS Number.MAX_SAFE_INTEGER
    // (2^53 - 1). The wasm path marshals the ms back through a double, so a larger
    // value couldn't round-trip exactly; capping here keeps this port bit-for-bit
    // identical to durationParser.js's Number.isSafeInteger checks (the parity twin).
    constexpr long long kMaxSafe = 9007199254740991LL;  // 2^53 - 1

    // Lower-case ASCII copy (durations are pure ASCII keywords/digits).
    std::string lower(const std::string& s) {
      std::string out;
      out.reserve(s.size());
      for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      return out;
    }

    // Split on runs of whitespace, dropping empties.
    std::vector<std::string> tokenize(const std::string& s) {
      std::vector<std::string> toks;
      std::size_t i = 0;
      while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        const std::size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > start) toks.push_back(s.substr(start, i - start));
      }
      return toks;
    }

    // Milliseconds for one unit word (singular or trailing-'s' plural). False on
    // an unknown word. Fixed durations, matching PERIOD_MS in projectsStore.
    bool unitMs(const std::string& word, long long& out) {
      const long long day = DurationParser::DAY_MS;
      std::string w = word;
      // Accept an optional plural 's' (days, weeks, months, years, fortnights).
      if (w.size() > 1 && w.back() == 's') w.pop_back();
      if (w == "day") { out = day; return true; }
      if (w == "week") { out = 7 * day; return true; }
      if (w == "fortnight") { out = 14 * day; return true; }
      if (w == "month") { out = 30 * day; return true; }
      if (w == "year") { out = 365 * day; return true; }
      return false;
    }

    // Parse a strictly-positive base-10 integer. False on empty, any non-digit,
    // or a value past kMaxSafe (matching JS Number.isSafeInteger).
    bool positiveInt(const std::string& s, long long& out) {
      if (s.empty()) return false;
      long long v = 0;
      for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        const int d = c - '0';
        if (v > (kMaxSafe - d) / 10) return false;  // overflow past 2^53 - 1
        v = v * 10 + d;
      }
      if (v <= 0) return false;
      out = v;
      return true;
    }

  }  // namespace

  bool DurationParser::parse(const std::string& spec, long long& outMs) const {
    const std::vector<std::string> toks = tokenize(lower(spec));
    if (toks.empty() || toks.size() > 2) return false;

    if (toks.size() == 1) {
      const std::string& t = toks[0];
      if (t == "off" || t == "never" || t == "none") { outMs = 0; return true; }
      long long unit = 0;
      if (unitMs(t, unit)) { outMs = unit; return true; }  // bare unit = one of it
      return false;
    }

    // Two tokens: a count and a unit, in either order.
    long long count = 0;
    long long unit = 0;
    if (positiveInt(toks[0], count) && unitMs(toks[1], unit)) {
      // count unit
    } else if (unitMs(toks[0], unit) && positiveInt(toks[1], count)) {
      // unit count
    } else {
      return false;
    }
    if (count > kMaxSafe / unit) return false;  // product past 2^53 - 1 (isSafeInteger parity)
    outMs = count * unit;
    return true;
  }

}
