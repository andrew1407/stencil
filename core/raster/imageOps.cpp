#include "imageOps.hpp"

#include "rgba.hpp"  // rgbaOffset, copyPixel

#include <algorithm>
#include <cstring>  // memcpy / memset

namespace stencil::core {

  namespace { constexpr int ROTATE_TILE = 32; }  // 32x32 RGBA8 = 4 KB/side, both stay hot

  int normalizeQuarters(int quarters) {
    return ((quarters % 4) + 4) % 4;
  }

  void rotatedDims(int w, int h, int quarters, int& outW, int& outH) {
    if (normalizeQuarters(quarters) % 2 == 0) { outW = w; outH = h; }
    else { outW = h; outH = w; }
  }

  void cropImageRows(const std::uint8_t* src, int srcW, int srcH, int rx, int ry,
                     int rw, int rh, std::uint8_t* dst, int dy0, int dy1) {
    if (src == nullptr || dst == nullptr || rw <= 0 || rh <= 0) return;
    dy0 = std::max(0, dy0);
    dy1 = std::min(rh, dy1);
    // Destination columns inside the source (64-bit: a far rect can't overflow).
    const long long lo = std::max(0LL, -static_cast<long long>(rx));
    const long long hi = std::min(static_cast<long long>(rw),
                                  static_cast<long long>(srcW) - rx);
    const int x0 = static_cast<int>(std::min<long long>(lo, rw));
    const int x1 = static_cast<int>(std::max<long long>(hi, 0));
    const std::size_t rowBytes = static_cast<std::size_t>(rw) * 4;
    for (int dy = dy0; dy < dy1; ++dy) {
      const int sy = ry + dy;
      std::uint8_t* o = dst + rgbaOffset(0, dy, rw);
      if (sy < 0 || sy >= srcH || x1 <= x0) {  // no source pixels on this row
        std::memset(o, 0, rowBytes);
        continue;
      }
      if (x0 > 0) std::memset(o, 0, static_cast<std::size_t>(x0) * 4);
      std::memcpy(o + static_cast<std::size_t>(x0) * 4, src + rgbaOffset(rx + x0, sy, srcW),
                  static_cast<std::size_t>(x1 - x0) * 4);
      if (x1 < rw) std::memset(o + static_cast<std::size_t>(x1) * 4, 0,
                               static_cast<std::size_t>(rw - x1) * 4);
    }
  }

  void cropImageRGBA(const std::uint8_t* src, int srcW, int srcH,
                     int rx, int ry, int rw, int rh, std::uint8_t* dst) {
    cropImageRows(src, srcW, srcH, rx, ry, rw, rh, dst, 0, rh);
  }

  void rotateImageRows(const std::uint8_t* src, int w, int h, int quarters,
                       std::uint8_t* dst, int oy0, int oy1) {
    if (src == nullptr || dst == nullptr || w <= 0 || h <= 0) return;
    const int q = normalizeQuarters(quarters);
    int outW = w, outH = h;
    rotatedDims(w, h, q, outW, outH);
    oy0 = std::max(0, oy0);
    oy1 = std::min(outH, oy1);
    if (q == 0) {  // identity: whole rows are contiguous on both sides
      for (int oy = oy0; oy < oy1; ++oy)
        std::memcpy(dst + rgbaOffset(0, oy, outW), src + rgbaOffset(0, oy, w),
                    static_cast<std::size_t>(w) * 4);
      return;
    }
    if (q == 2) {  // 180°: source row reversed
      for (int oy = oy0; oy < oy1; ++oy) {
        std::uint8_t* o = dst + rgbaOffset(0, oy, outW);
        const std::uint8_t* s = src + rgbaOffset(w - 1, h - 1 - oy, w);
        for (int ox = 0; ox < outW; ++ox, o += 4, s -= 4) copyPixel(o, s);
      }
      return;
    }
    // 90°/270°: a transpose — source-order writes would scatter a row apart, so tile.
    for (int oyb = oy0; oyb < oy1; oyb += ROTATE_TILE) {
      const int oyEnd = std::min(oyb + ROTATE_TILE, oy1);
      for (int oxb = 0; oxb < outW; oxb += ROTATE_TILE) {
        const int oxEnd = std::min(oxb + ROTATE_TILE, outW);
        for (int oy = oyb; oy < oyEnd; ++oy) {
          // dst(ox,oy) reads one src column, walking y with the destination x.
          const std::uint8_t* col = src + rgbaOffset(q == 1 ? oy : w - 1 - oy, 0, w);
          const std::size_t stride = static_cast<std::size_t>(w) * 4;
          std::uint8_t* o = dst + rgbaOffset(oxb, oy, outW);
          for (int ox = oxb; ox < oxEnd; ++ox, o += 4)
            copyPixel(o, col + static_cast<std::size_t>(q == 1 ? h - 1 - ox : ox) * stride);
        }
      }
    }
  }

  void rotateImageRGBA(const std::uint8_t* src, int w, int h, int quarters,
                       std::uint8_t* dst) {
    int outW = w, outH = h;
    rotatedDims(w, h, quarters, outW, outH);
    rotateImageRows(src, w, h, quarters, dst, 0, outH);
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
