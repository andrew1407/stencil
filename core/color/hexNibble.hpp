#pragma once

// The one nibble decoder behind both colour parsers (color.cpp, colorNames.cpp).
// Header-only, so it needs no source-list sync across the three builds.
namespace stencil::core {

  inline int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

}  // namespace stencil::core
