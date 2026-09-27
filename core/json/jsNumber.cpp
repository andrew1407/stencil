#include "jsNumber.hpp"

#include "decimal.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace stencil::core::json {

  namespace {
    // A candidate decimal: `digits` × 10^(exp10 − digits.size() + 1), exp10 the lead digit's power.
    struct Decimal {
      std::string digits;
      int exp10 = 0;
    };

    // The correctly rounded `precision`-digit decimal of x (printf's %e rounds exactly).
    Decimal rounded(double x, int precision) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "%.*e", precision - 1, x);
      Decimal d;
      std::size_t i = 0;
      for (; buf[i] != '\0' && buf[i] != 'e'; ++i)
        if (buf[i] >= '0' && buf[i] <= '9') d.digits.push_back(buf[i]);
      int sign = 1, e = 0;
      if (buf[i] == 'e') ++i;
      if (buf[i] == '-' || buf[i] == '+') sign = buf[i++] == '-' ? -1 : 1;
      for (; buf[i] >= '0' && buf[i] <= '9'; ++i) e = e * 10 + (buf[i] - '0');
      d.exp10 = sign * e;
      return d;
    }

    double valueOf(const Decimal& d) {
      const int scale = d.exp10 - static_cast<int>(d.digits.size()) + 1;
      const auto v = parseDecimal(d.digits + "e" + std::to_string(scale));
      return v ? *v : NAN;
    }

    // One unit in the last digit up or down, staying at the same digit count: 100…0 steps
    // down to 99…9 a decade lower, 99…9 steps up to 100…0 a decade higher.
    Decimal step(Decimal d, int dir) {
      std::string& s = d.digits;
      if (dir > 0) {
        std::size_t i = s.size();
        while (i > 0 && s[i - 1] == '9') s[--i] = '0';
        if (i == 0) { s.insert(s.begin(), '1'); s.pop_back(); ++d.exp10; }
        else ++s[i - 1];
      } else if (s[0] == '1' && s.find_first_not_of('0', 1) == std::string::npos) {
        s.assign(s.size(), '9');
        --d.exp10;
      } else {
        std::size_t i = s.size();
        while (s[i - 1] == '0') s[--i] = '9';
        --s[i - 1];
      }
      return d;
    }

    // ECMA-262 Number::toString: the fewest digits k whose decimal rounds back to x. Of the
    // k-digit decimals, the nearest one is tried first, then its neighbour across x.
    Decimal shortest(double x) {
      Decimal best;
      for (int k = 1; k <= 17; ++k) {
        best = rounded(x, k);
        const double back = valueOf(best);
        if (back == x) break;
        const Decimal other = step(best, back < x ? 1 : -1);
        if (valueOf(other) == x) { best = other; break; }
      }
      while (best.digits.size() > 1 && best.digits.back() == '0') best.digits.pop_back();
      return best;
    }
  }  // namespace

  std::string jsNumberToString(double x) {
    if (std::isnan(x)) return "NaN";
    if (x == 0) return "0";
    if (std::isinf(x)) return x < 0 ? "-Infinity" : "Infinity";
    if (x < 0) return "-" + jsNumberToString(-x);
    const Decimal d = shortest(x);
    const std::string& s = d.digits;
    const int k = static_cast<int>(s.size());
    const int n = d.exp10 + 1;
    if (k <= n && n <= 21) return s + std::string(static_cast<std::size_t>(n - k), '0');
    if (0 < n && n <= 21) return s.substr(0, static_cast<std::size_t>(n)) + "." + s.substr(static_cast<std::size_t>(n));
    if (-6 < n && n <= 0) return "0." + std::string(static_cast<std::size_t>(-n), '0') + s;
    const std::string exponent = (n - 1 < 0 ? "-" : "+") + std::to_string(n - 1 < 0 ? 1 - n : n - 1);
    if (k == 1) return s + "e" + exponent;
    return s.substr(0, 1) + "." + s.substr(1) + "e" + exponent;
  }

}  // namespace stencil::core::json
