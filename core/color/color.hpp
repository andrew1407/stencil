#pragma once
#include <optional>
#include <string>

// Hex colour parsing. Port of parseHex in browser/js/utils.js.
namespace stencil::core {

  struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
  };

  // nullopt unless a 7-char "#rrggbb".
  std::optional<Rgb> parseHex(const std::string& hex);

}
