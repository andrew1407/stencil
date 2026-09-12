#pragma once
#include <cstdint>

// The two Rec. 709 luma reductions the core's filters use. They are deliberately
// NOT interchangeable and must never be merged: each is pinned byte-for-byte
// against its own JS fallback twin, and they can disagree by one. Named here so
// the split reads as a decision rather than as two stray copies.
namespace stencil::core::luma {

  // Filter path (imageFilter filterPixel ← renderer.js grayscale(100%)): float
  // weights on 0..255 channels, truncated toward zero.
  inline int rec709Truncated(int r, int g, int b) {
    return static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b);
  }

  // Contour path (the Sobel pre-pass ← contourFilter.js): integer weights summing
  // to 10000, so the quotient is bounded 0..255 and no float enters the loop.
  inline std::uint8_t rec709Scaled(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint8_t>((2126 * r + 7152 * g + 722 * b) / 10000);
  }

}
