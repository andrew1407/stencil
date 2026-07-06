#pragma once
#include <cstddef>
#include <cstdint>

// Tiny helpers for the tightly-packed, row-major RGBA8 buffers the core moves
// over its flat ABI (4 bytes/pixel, no stride padding). They centralize the
// `(y*w + x)*4` index math and the 4-byte pixel copy that were spelled out by
// hand across the image ops (geometry/imageOps, geometry/rasterize, …).
//
// Header-only on purpose: trivial and shared by several TUs, so keeping them
// inline avoids adding a .cpp that would have to be mirrored across the three
// build definitions (core/CMakeLists.txt, cli/build.zig, pystencil/build.py).
namespace stencil::core {

  inline std::size_t rgbaOffset(int x, int y, int w) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
            static_cast<std::size_t>(x)) *
           4;
  }

  inline void copyPixel(std::uint8_t* dst, const std::uint8_t* src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
  }

}  // namespace stencil::core
