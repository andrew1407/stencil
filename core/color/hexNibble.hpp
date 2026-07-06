#pragma once

// One hexadecimal digit → its value 0–15, or -1 if `c` is not a hex digit.
// Case-insensitive. Shared by the two colour parsers — color.cpp's strict #rrggbb
// path (the utils.js twin) and colorNames.cpp's #rgb/#rgba/#rrggbb/#rrggbbaa +
// names path (the headless resolver) — so the nibble math lives in one place.
// Header-only (no .cpp), so it needs no source-list sync across the three builds.
namespace stencil::core {

  inline int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

}  // namespace stencil::core
