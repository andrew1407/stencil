#pragma once
#include <cctype>
#include <string>
#include <string_view>

// ASCII string helpers. Header-only on purpose: a .cpp would have to be mirrored across
// the three build definitions. ASCII-only by design — the core's tokens (colour names,
// units, keys) are ASCII, matching the browser's `.toLowerCase()` / `.trim()`.
namespace stencil::core {

  inline std::string toLowerAscii(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in)
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
  }

  inline std::string_view trimAscii(std::string_view in) {
    std::size_t a = 0, b = in.size();
    while (a < b && std::isspace(static_cast<unsigned char>(in[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;
    return in.substr(a, b - a);
  }

  inline std::string trimLowerAscii(std::string_view in) {
    return toLowerAscii(trimAscii(in));
  }

}  // namespace stencil::core
