#include "downscale.hpp"

#include "rgba.hpp"  // rgbaOffset, RGBA_BYTES

#include <algorithm>
#include <cstddef>
#include <vector>

namespace stencil::core {

  namespace {

    struct Tap {
      int index;
      std::uint64_t weight;
    };

    // One axis: output o spans [o*src, (o+1)*src) and input i spans [i*dst, (i+1)*dst), in
    // units of 1/(src*dst) of the axis, so every overlap is an integer and o's weights sum to src.
    struct Axis {
      std::vector<Tap> taps;
      std::vector<std::size_t> first;  // taps of output o: [first[o], first[o+1])

      Axis(int src, int dst) : first(static_cast<std::size_t>(dst) + 1, 0) {
        for (std::int64_t o = 0; o < dst; ++o) {
          first[static_cast<std::size_t>(o)] = taps.size();
          const std::int64_t lo = o * src;
          const std::int64_t hi = lo + src;
          for (std::int64_t i = lo / dst; i * dst < hi; ++i) {
            const std::int64_t w = std::min((i + 1) * dst, hi) - std::max(i * dst, lo);
            taps.push_back({static_cast<int>(i), static_cast<std::uint64_t>(w)});
          }
        }
        first[static_cast<std::size_t>(dst)] = taps.size();
      }
    };

    // One source row folded onto the output columns: per column, sum(w*a*r), sum(w*a*g),
    // sum(w*a*b), sum(w*a).
    void foldRow(const std::uint8_t* line, const Axis& x, std::vector<std::uint64_t>& out) {
      const std::size_t cols = x.first.size() - 1;
      for (std::size_t o = 0; o < cols; ++o) {
        std::uint64_t r = 0, g = 0, b = 0, a = 0;
        for (std::size_t t = x.first[o]; t < x.first[o + 1]; ++t) {
          const std::uint8_t* p = line + static_cast<std::size_t>(x.taps[t].index) * RGBA_BYTES;
          const std::uint64_t wa = x.taps[t].weight * p[3];
          r += wa * p[0];
          g += wa * p[1];
          b += wa * p[2];
          a += wa;
        }
        std::uint64_t* s = &out[o * RGBA_BYTES];
        s[0] = r;
        s[1] = g;
        s[2] = b;
        s[3] = a;
      }
    }

    // alpha = sum(w*a) / area; colour = sum(w*a*c) / sum(w*a), both rounded half up. The sums
    // stay below area * 255^2, which fits 64 bits for any image a codec can hold.
    void writePixel(const std::uint64_t* sum, std::uint64_t area, std::uint8_t* out) {
      const std::uint64_t alpha = sum[3];
      out[3] = static_cast<std::uint8_t>((alpha + area / 2) / area);
      for (int c = 0; c < 3; ++c) {
        out[c] = alpha == 0 ? 0 : static_cast<std::uint8_t>((sum[c] + alpha / 2) / alpha);
      }
    }

  }  // namespace

  void thumbnailDims(int w, int h, int maxSide, int& outW, int& outH) {
    outW = w;
    outH = h;
    const int longer = std::max(w, h);
    if (maxSide < 1 || w < 1 || h < 1 || longer <= maxSide) return;
    const auto scaled = [&](int side) {
      const std::int64_t v = (static_cast<std::int64_t>(side) * maxSide + longer / 2) / longer;
      return static_cast<int>(std::max<std::int64_t>(1, v));
    };
    outW = w == longer ? maxSide : scaled(w);
    outH = w == longer ? scaled(h) : maxSide;
  }

  bool downscaleRGBA(const std::uint8_t* src, int srcW, int srcH, std::uint8_t* dst,
                     int dstW, int dstH) {
    if (src == nullptr || dst == nullptr || dstW < 1 || dstH < 1 || dstW > srcW ||
        dstH > srcH) {
      return false;
    }
    if (dstW == srcW && dstH == srcH) {
      std::copy(src, src + rgbaOffset(0, srcH, srcW), dst);
      return true;
    }
    const Axis x(srcW, dstW);
    const Axis y(srcH, dstH);
    const std::size_t rowSums = static_cast<std::size_t>(dstW) * RGBA_BYTES;
    std::vector<std::uint64_t> row(rowSums);
    std::vector<std::uint64_t> acc(rowSums);
    const std::uint64_t area = static_cast<std::uint64_t>(srcW) * static_cast<std::uint64_t>(srcH);
    for (int oy = 0; oy < dstH; ++oy) {
      std::fill(acc.begin(), acc.end(), 0);
      const auto o = static_cast<std::size_t>(oy);
      for (std::size_t t = y.first[o]; t < y.first[o + 1]; ++t) {
        foldRow(src + rgbaOffset(0, y.taps[t].index, srcW), x, row);
        for (std::size_t k = 0; k < rowSums; ++k) acc[k] += y.taps[t].weight * row[k];
      }
      for (int ox = 0; ox < dstW; ++ox) {
        writePixel(&acc[static_cast<std::size_t>(ox) * RGBA_BYTES], area,
                   dst + rgbaOffset(ox, oy, dstW));
      }
    }
    return true;
  }

}  // namespace stencil::core
