#pragma once
#include <cstddef>
#include <cstdint>

// Index math for the core's row-major RGBA8 buffers (4 bytes/pixel, no stride padding).
// Header-only on purpose: a .cpp would have to be mirrored across the three build
// definitions (core/CMakeLists.txt, cli/build.zig, pystencil/build.py).
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
