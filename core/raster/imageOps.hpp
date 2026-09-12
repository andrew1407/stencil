#pragma once
#include <cstddef>
#include <cstdint>

// Whole-image RGBA8 transforms for the headless pipeline over caller-owned buffers
// (the Zig CLI allocates them); byte order R,G,B,A like imageFilter.hpp.
namespace stencil::core {

  // Signed quarter-turn count -> 0..3 clockwise (-1 -> 3, 5 -> 1).
  int normalizeQuarters(int quarters);

  void rotatedDims(int w, int h, int quarters, int& outW, int& outH);

  // dst holds rw*rh*4 bytes; rect pixels outside the source are written transparent,
  // so the rect may exceed the image bounds.
  void cropImageRGBA(const std::uint8_t* src, int srcW, int srcH,
                     int rx, int ry, int rw, int rh, std::uint8_t* dst);

  // Clockwise; dst holds rotatedDims(w,h,quarters)*4 bytes.
  void rotateImageRGBA(const std::uint8_t* src, int w, int h, int quarters,
                       std::uint8_t* dst);

  // Half-open [dy0, dy1) DESTINATION-row slices for an adapter's pool (core owns no
  // threading); disjoint rows, so ranges may run concurrently.
  void cropImageRows(const std::uint8_t* src, int srcW, int srcH, int rx, int ry,
                     int rw, int rh, std::uint8_t* dst, int dy0, int dy1);
  void rotateImageRows(const std::uint8_t* src, int w, int h, int quarters,
                       std::uint8_t* dst, int oy0, int oy1);

  void fillRGBA(std::uint8_t* dst, std::size_t pixelCount, int r, int g, int b, int a);

}
