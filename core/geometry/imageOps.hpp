#pragma once
#include <cstddef>
#include <cstdint>

// Whole-image RGBA8 buffer transforms for the headless pipeline: crop, quarter-turn
// rotation, and solid fill. GUI-free and codec-free — buffers are caller-owned
// (the Zig CLI allocates them via zigimg); core only moves bytes. Byte order is
// R,G,B,A interleaved, matching imageFilter.hpp and a browser ImageData.
namespace stencil::core {

  // Normalize a signed quarter-turn count to 0..3 (clockwise). e.g. -1 -> 3, 5 -> 1.
  int normalizeQuarters(int quarters);

  // Output dimensions after rotating a w x h image by `quarters` quarter-turns
  // (odd turns swap width/height).
  void rotatedDims(int w, int h, int quarters, int& outW, int& outH);

  // Copy an axis-aligned sub-rectangle (rx,ry,rw,rh) of an RGBA8 source into dst,
  // which must hold rw*rh*4 bytes. Pixels of the rect that fall outside the source
  // are written transparent (0,0,0,0), so the rect may exceed the image bounds.
  void cropImageRGBA(const std::uint8_t* src, int srcW, int srcH,
                     int rx, int ry, int rw, int rh, std::uint8_t* dst);

  // Rotate an RGBA8 image by `quarters` quarter-turns clockwise into dst, which must
  // hold rotatedDims(w,h,quarters)*4 bytes.
  void rotateImageRGBA(const std::uint8_t* src, int w, int h, int quarters,
                       std::uint8_t* dst);

  // Fill an RGBA8 buffer of `pixelCount` pixels with one colour.
  void fillRGBA(std::uint8_t* dst, std::size_t pixelCount, int r, int g, int b, int a);

}
