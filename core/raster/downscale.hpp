#pragma once
#include <cstdint>

// Area-average (box) RGBA8 downscale for the adapters' thumbnails (the CLI's --thumbnail).
// No browser call site, so no JS twin: like the line rasteriser, it is core-only. Byte order
// R,G,B,A over caller-owned buffers, like imageOps.hpp.
namespace stencil::core {

  // The size whose longer side is at most maxSide, aspect kept, the shorter side rounded
  // and at least 1. Never upscales: an image that already fits, or maxSide < 1, keeps w x h.
  void thumbnailDims(int w, int h, int maxSide, int& outW, int& outH);

  // dst (dstW*dstH*4 bytes) gets each covered source area's mean, premultiplied so a transparent
  // pixel adds no colour. False, dst untouched, unless 1 <= dstW <= srcW and 1 <= dstH <= srcH.
  bool downscaleRGBA(const std::uint8_t* src, int srcW, int srcH, std::uint8_t* dst,
                     int dstW, int dstH);

}
