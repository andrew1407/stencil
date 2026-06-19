#include "color.hpp"
#include <cctype>
#include <cmath>
#include <sstream>

namespace stencil::core {

  namespace {
    // Two hex digits -> int, or -1 if either char is not a hex digit.
    int hexByte(char hi, char lo) {
      auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (l >= 'a' && l <= 'f') return 10 + (l - 'a');
        return -1;
      };
      const int h = nib(hi);
      const int l = nib(lo);
      if (h < 0 || l < 0) return -1;
      return h * 16 + l;
    }

    // Format a double the way JS string interpolation would for typical alphas:
    // integers print without a decimal point, fractions drop trailing zeros.
    std::string trimNumber(double v) {
      std::ostringstream os;
      os << v;
      return os.str();
    }
  }

  std::optional<Rgb> parseHex(const std::string& hex) {
    if (hex.size() < 7 || hex[0] != '#') return std::nullopt;
    const int r = hexByte(hex[1], hex[2]);
    const int g = hexByte(hex[3], hex[4]);
    const int b = hexByte(hex[5], hex[6]);
    if (r < 0 || g < 0 || b < 0) return std::nullopt;
    return Rgb{r, g, b};
  }

  std::string hexToRgba(const std::string& hex, double alpha) {
    const auto rgb = parseHex(hex);
    if (!rgb) return hex;  // pass through values already rgba/named
    std::ostringstream os;
    os << "rgba(" << rgb->r << ',' << rgb->g << ',' << rgb->b << ','
       << trimNumber(alpha) << ')';
    return os.str();
  }

}
