#include "durationParser.hpp"

#include "text.hpp"  // toLowerAscii

#include <cctype>
#include <vector>

namespace stencil::core {

  namespace {

    // Upper bound on a count and on the resulting ms — JS Number.MAX_SAFE_INTEGER
    // (2^53 - 1). The wasm path marshals the ms back through a double, so a larger
    // value couldn't round-trip exactly; capping here keeps this port bit-for-bit
    // identical to durationParser.js's Number.isSafeInteger checks (the parity twin).
    constexpr long long kMaxSafe = 9007199254740991LL;  // 2^53 - 1

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

    // The grammar's vocabulary, in help order. One table so the words a spec may
    // use and the words unitNames()/offAliases() advertise can never drift apart.
    // Fixed durations, matching PERIOD_MS in projectsStore.
    struct Unit {
      const char* name;
      long long days;
    };
    constexpr Unit kUnits[] = {{"day", 1}, {"week", 7}, {"fortnight", 14},
                               {"month", 30}, {"year", 365}};
    constexpr const char* kOffAliases[] = {"off", "never", "none"};

    // Milliseconds for one unit word (singular or trailing-'s' plural). False on
    // an unknown word.
    bool unitMs(const std::string& word, long long& out) {
      std::string w = word;
      // Accept an optional plural 's' (days, weeks, months, years, fortnights).
      if (w.size() > 1 && w.back() == 's') w.pop_back();
      for (const Unit& u : kUnits)
        if (w == u.name) { out = u.days * DurationParser::DAY_MS; return true; }
      return false;
    }

    bool isOffAlias(const std::string& w) {
      for (const char* a : kOffAliases)
        if (w == a) return true;
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

  const char* DurationParser::unitNames() {
    static const std::string s = [] {
      std::string out;
      for (const Unit& u : kUnits) {
        if (!out.empty()) out += ' ';
        out += u.name;
      }
      return out;
    }();
    return s.c_str();
  }

  const char* DurationParser::offAliases() {
    static const std::string s = [] {
      std::string out;
      for (const char* a : kOffAliases) {
        if (!out.empty()) out += ' ';
        out += a;
      }
      return out;
    }();
    return s.c_str();
  }

  bool DurationParser::parse(const std::string& spec, long long& outMs) const {
    const std::vector<std::string> toks = tokenize(toLowerAscii(spec));
    if (toks.empty() || toks.size() > 2) return false;

    if (toks.size() == 1) {
      const std::string& t = toks[0];
      if (isOffAlias(t)) { outMs = 0; return true; }
      long long unit = 0;
      if (unitMs(t, unit)) { outMs = unit; return true; }  // bare unit = one of it
      return false;
    }

    // Two tokens: a count and a unit, in either order.
    long long count = 0;
    long long unit = 0;
    if (positiveInt(toks[0], count) && unitMs(toks[1], unit)) {
    } else if (unitMs(toks[0], unit) && positiveInt(toks[1], count)) {
    } else {
      return false;
    }
    if (count > kMaxSafe / unit) return false;  // product past 2^53 - 1 (isSafeInteger parity)
    outMs = count * unit;
    return true;
  }

}
