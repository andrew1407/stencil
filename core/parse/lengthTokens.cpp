#include "lengthTokens.hpp"

#include "text.hpp"

#include <cctype>
#include <cstdlib>

namespace stencil::core {

  namespace {
    constexpr double kCmPerInch = 2.54;  // mirrors CM_PER_INCH in browser/js/utils.js
  }  // namespace

  // Hand-rolled equivalent of /^(-)?\s*(\d*\.?\d+)\s*(px|cm|mm|in|%)?$/ — the core
  // avoids <regex> (see formulaParser) for speed and small wasm output.
  std::optional<LengthToken> parseLengthToken(const std::string& token) {
    // Trim + lowercase, matching the JS `token.trim().toLowerCase()`.
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
    if (unit == "%") {
      t.kind = LengthKind::Percent; t.value = value; t.fromEnd = fromEnd;
    } else if (unit == "cm") {
      t.kind = LengthKind::Cm; t.value = value; t.fromEnd = fromEnd;
    } else if (unit == "mm") {
      t.kind = LengthKind::Cm; t.value = value / 10.0; t.fromEnd = fromEnd;
    } else if (unit == "in") {
      t.kind = LengthKind::Cm; t.value = value * kCmPerInch; t.fromEnd = fromEnd;
    } else if (unit == "px") {
      t.kind = LengthKind::Px; t.value = value; t.fromEnd = fromEnd;
    } else if (unit.empty()) {
      // A bare number is a delta — keep the sign.
      t.kind = LengthKind::Delta; t.value = fromEnd ? -value : value; t.fromEnd = false;
    } else {
      return std::nullopt;  // unknown unit suffix
    }
    return t;
  }

  std::optional<double> resolveAxisPx(const std::string& token, double lengthPx,
                                      double pxPerCm, double currentPx) {
    const auto t = parseLengthToken(token);
    if (!t) return std::nullopt;
    if (t->kind == LengthKind::Delta) return currentPx + t->value;

    double px;
    if (t->kind == LengthKind::Px) px = t->value;
    else if (t->kind == LengthKind::Cm) px = t->value * pxPerCm;
    else px = (t->value / 100.0) * lengthPx;  // percent

    return t->fromEnd ? lengthPx - px : px;
  }

}  // namespace stencil::core
