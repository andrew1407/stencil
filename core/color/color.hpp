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

  // nullopt unless a 7-char "#rrggbb".
  std::optional<Rgb> parseHex(const std::string& hex);

  // "rgba(r,g,b,a)"; anything but a 7-char hex passes through unchanged, like the JS.
  std::string hexToRgba(const std::string& hex, double alpha);

}
