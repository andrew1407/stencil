#pragma once
#include <optional>
#include <string>

// Color helpers. Port of the color section of browser/js/utils.js.
namespace stencil::core {

  struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
  };

  // Parse "#rrggbb" -> Rgb. Returns nullopt if the string is not a 7-char hex.
  std::optional<Rgb> parseHex(const std::string& hex);

  // "#rrggbb" + alpha -> "rgba(r,g,b,a)". If `hex` is not a valid 7-char hex,
  // it is returned unchanged (mirrors the JS pass-through behavior).
  std::string hexToRgba(const std::string& hex, double alpha);

}
