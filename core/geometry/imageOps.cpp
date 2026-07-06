#include "imageOps.hpp"

#include "rgba.hpp"  // rgbaOffset, copyPixel

namespace stencil::core {

  int normalizeQuarters(int quarters) {
    return ((quarters % 4) + 4) % 4;
  }

  void rotatedDims(int w, int h, int quarters, int& outW, int& outH) {
    if (normalizeQuarters(quarters) % 2 == 0) { outW = w; outH = h; }
    else { outW = h; outH = w; }
  }

  void cropImageRGBA(const std::uint8_t* src, int srcW, int srcH,
                     int rx, int ry, int rw, int rh, std::uint8_t* dst) {
    if (rw <= 0 || rh <= 0) return;
    for (int dy = 0; dy < rh; ++dy) {
      const int sy = ry + dy;
      for (int dx = 0; dx < rw; ++dx) {
        const int sx = rx + dx;
        std::uint8_t* o = dst + rgbaOffset(dx, dy, rw);
        if (sx >= 0 && sx < srcW && sy >= 0 && sy < srcH) {
          copyPixel(o, src + rgbaOffset(sx, sy, srcW));
        } else {
          o[0] = o[1] = o[2] = o[3] = 0;  // outside the source -> transparent
        }
      }
    }
  }

  void rotateImageRGBA(const std::uint8_t* src, int w, int h, int quarters,
                       std::uint8_t* dst) {
    const int q = normalizeQuarters(quarters);
    int outW = w, outH = h;
    rotatedDims(w, h, q, outW, outH);

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        int ox = x;
        int oy = y;
        switch (q) {
          case 1:  // 90° clockwise
            ox = h - 1 - y;
            oy = x;
            break;
          case 2:  // 180°
            ox = w - 1 - x;
            oy = h - 1 - y;
            break;
          case 3:  // 270° clockwise
            ox = y;
            oy = w - 1 - x;
            break;
          default:  // 0° — identity
            break;
        }
        copyPixel(dst + rgbaOffset(ox, oy, outW), src + rgbaOffset(x, y, w));
      }
    }
  }

  void fillRGBA(std::uint8_t* dst, std::size_t pixelCount, int r, int g, int b, int a) {
    for (std::size_t i = 0; i < pixelCount; ++i) {
      std::uint8_t* o = dst + i * 4;
      o[0] = static_cast<std::uint8_t>(r);
      o[1] = static_cast<std::uint8_t>(g);
      o[2] = static_cast<std::uint8_t>(b);
      o[3] = static_cast<std::uint8_t>(a);
    }
  }

}  // namespace stencil::core
