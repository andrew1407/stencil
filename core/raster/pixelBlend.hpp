#pragma once
#include "colorNames.hpp"  // Rgba
#include "rgba.hpp"        // rgbaOffset, CHANNEL_MAX

#include <cstdint>

// The one source-over blend the line rasteriser writes through, and its scan-bound clamp.
// Header-only: rasterize.cpp and strokeCoverage.cpp inline it per pixel.
namespace stencil::core::blend {

  // `!(v >= lo)` also catches NaN. Clamping scan bounds is output-preserving (blendPixel
  // skips out-of-bounds writes) and caps the loop length of a far-off or huge shape.
  inline int clampToInt(double v, int lo, int hi) {
    if (!(v >= static_cast<double>(lo))) return lo;
    if (v > static_cast<double>(hi)) return hi;
    return static_cast<int>(v);
  }

  // round(v / 255) for v in [0, 65535], without a divide. Exact over that range,
  // which covers c*a + d*(255-a) since the two weights sum to 255 (max 255*255).
  inline std::uint8_t div255(int v) {
    v += 128;  // round-to-nearest bias
    return static_cast<std::uint8_t>((v + (v >> 8)) >> 8);
  }

  // Source-over with `coverage` (0..1) folded into the colour's alpha, quantised to 8
  // bits; out-of-bounds writes are ignored.
  inline void blendPixel(std::uint8_t* buf, int w, int h, int x, int y, const Rgba& c,
                         double coverage) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    int a = static_cast<int>(coverage * c.a + 0.5);  // effective alpha, 0..255
    if (a <= 0) return;
    if (a > CHANNEL_MAX) a = CHANNEL_MAX;
    const int ia = CHANNEL_MAX - a;
    std::uint8_t* p = buf + rgbaOffset(x, y, w);
    p[0] = div255(c.r * a + p[0] * ia);
    p[1] = div255(c.g * a + p[1] * ia);
    p[2] = div255(c.b * a + p[2] * ia);
    p[3] = div255(CHANNEL_MAX * a + p[3] * ia);
  }

}  // namespace stencil::core::blend
