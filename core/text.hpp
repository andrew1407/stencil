#pragma once
#include <cctype>
#include <string>
#include <string_view>

// Small ASCII string helpers shared across the core. Header-only on purpose: these are
// trivial and used by several groups (color/, parse/, state/), and keeping them inline
// avoids adding a source file to BOTH core/CMakeLists.txt and cli/build.zig (which must
// stay in sync). ASCII-only by design — the core's tokens (colour names, units, keys)
// are ASCII, matching the browser's `.toLowerCase()` / `.trim()` reference behaviour.
namespace stencil::core {

  // ASCII-lowercase a copy of `in`.
  inline std::string toLowerAscii(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in)
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
  }

  // View of `in` with leading/trailing ASCII whitespace removed (no allocation).
  inline std::string_view trimAscii(std::string_view in) {
    std::size_t a = 0, b = in.size();
    while (a < b && std::isspace(static_cast<unsigned char>(in[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;
    return in.substr(a, b - a);
  }

  // Trim then ASCII-lowercase — the common "normalize a token" step.
  inline std::string trimLowerAscii(std::string_view in) {
    return toLowerAscii(trimAscii(in));
  }

}  // namespace stencil::core
