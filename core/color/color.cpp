#include "color.hpp"

#include "hexNibble.hpp"

namespace stencil::core {

  namespace {
    int hexByte(char hi, char lo) {
      const int h = hexNibble(hi);
      const int l = hexNibble(lo);
      if (h < 0 || l < 0) return -1;
      return h * 16 + l;
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

}
