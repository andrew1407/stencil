#include "DurationParser.hpp"

#include "text.hpp"  // toLowerAscii

#include <cctype>
#include <vector>

namespace stencil::core {

  namespace {

    // JS Number.MAX_SAFE_INTEGER: the wasm path marshals ms through a double, and the
    // cap keeps this port identical to durationParser.js's Number.isSafeInteger checks.
    constexpr long long MAX_SAFE = 9007199254740991LL;  // 2^53 - 1

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

    // One table in help order, so the words parse() accepts and the words
    // unitNames()/offAliases() advertise cannot drift apart.
    struct Unit {
      const char* name;
      long long days;
    };
    constexpr Unit UNITS[] = {{"day", 1}, {"week", 7}, {"fortnight", 14},
                               {"month", 30}, {"year", 365}};
    constexpr const char* OFF_ALIASES[] = {"off", "never", "none"};

    bool unitMs(const std::string& word, long long& out) {
      std::string w = word;
      if (w.size() > 1 && w.back() == 's') w.pop_back();
      for (const Unit& u : UNITS)
        if (w == u.name) { out = u.days * DurationParser::DAY_MS; return true; }
      return false;
    }

    bool isOffAlias(const std::string& w) {
      for (const char* a : OFF_ALIASES)
        if (w == a) return true;
      return false;
    }

    // Strictly positive, and capped at MAX_SAFE.
    bool positiveInt(const std::string& s, long long& out) {
      if (s.empty()) return false;
      long long v = 0;
      for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        const int d = c - '0';
        if (v > (MAX_SAFE - d) / 10) return false;  // overflow past 2^53 - 1
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
      for (const Unit& u : UNITS) {
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
      for (const char* a : OFF_ALIASES) {
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
    if (count > MAX_SAFE / unit) return false;  // product past 2^53 - 1 (isSafeInteger parity)
    outMs = count * unit;
    return true;
  }

}
