#include "lengthTokens.hpp"

#include "text.hpp"

#include <array>
#include <cctype>
#include <cstdlib>

namespace stencil::core {

  namespace {
    constexpr double CM_PER_INCH = 2.54;  // mirrors CM_PER_INCH in browser/js/utils.js

    // value * mul / div, not one scale factor: mm is a DIVISION by 10, and x * 0.1 is not
    // the same double as x / 10 for every x.
    struct UnitSpec {
      LengthKind kind;
      double mul;
      double div;
    };

    constexpr std::array<Keyed<UnitSpec>, 5> LENGTH_UNITS = {{
        {"%", {LengthKind::PERCENT, 1.0, 1.0}},
        {"cm", {LengthKind::CM, 1.0, 1.0}},
        {"mm", {LengthKind::CM, 1.0, 10.0}},
        {"in", {LengthKind::CM, CM_PER_INCH, 1.0}},
        {"px", {LengthKind::PX, 1.0, 1.0}},
    }};
  }  // namespace

  // Hand-rolled /^(-)?\s*(\d*\.?\d+)\s*(px|cm|mm|in|%)?$/ — no <regex> in the wasm build.
  std::optional<LengthToken> parseLengthToken(const std::string& token) {
    const std::string s = trimLowerAscii(token);
    if (s.empty()) return std::nullopt;

    std::size_t i = 0;
    bool fromEnd = false;
    if (s[i] == '-') { fromEnd = true; ++i; }
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;

    // Number: \d*\.?\d+ (at least one digit, at most one dot, no trailing dot).
    const std::size_t numStart = i;
    int dots = 0, digitsAfterDot = 0, digits = 0;
    while (i < s.size()) {
      const char c = s[i];
      if (c >= '0' && c <= '9') {
        ++digits;
        if (dots) ++digitsAfterDot;
        ++i;
      } else if (c == '.') {
        if (dots) break;
        ++dots;
        ++i;
      } else {
        break;
      }
    }
    if (digits == 0) return std::nullopt;                 // no number at all
    if (dots > 0 && digitsAfterDot == 0) return std::nullopt;  // trailing dot ("5.")

    const double value = std::strtod(s.c_str() + numStart, nullptr);

    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    const std::string unit = s.substr(i);

    LengthToken t;
    t.fromEnd = fromEnd;  // every kind but Delta keeps it; Delta folds it into the sign below
    if (unit.empty()) {
      // A bare number is a delta — keep the sign.
      t.kind = LengthKind::DELTA;
      t.value = fromEnd ? -value : value;
      t.fromEnd = false;
      return t;
    }
    const UnitSpec* u = lookupPtr(LENGTH_UNITS, unit);
    if (u == nullptr) return std::nullopt;  // unknown unit suffix
    t.kind = u->kind;
    t.value = value * u->mul / u->div;
    return t;
  }

  std::optional<double> resolveAxisPx(const std::string& token, double lengthPx,
                                      double pxPerCm, double currentPx) {
    const auto t = parseLengthToken(token);
    if (!t) return std::nullopt;
    if (t->kind == LengthKind::DELTA) return currentPx + t->value;

    double px;
    if (t->kind == LengthKind::PX) px = t->value;
    else if (t->kind == LengthKind::CM) px = t->value * pxPerCm;
    else px = (t->value / 100.0) * lengthPx;  // percent

    return t->fromEnd ? lengthPx - px : px;
  }

}  // namespace stencil::core
