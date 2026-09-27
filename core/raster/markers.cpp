#include "markers.hpp"

#include "pixelBlend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace stencil::core::markers {

  namespace {

    using blend::blendPixel;
    using blend::clampToInt;

    // A marker's full-coverage run up to this many columns is blended in place; a longer one is
    // counted, and each column takes its owed blends when an edge reaches it or the row ends.
    constexpr int DIRECT_RUN = 32;

    struct Marker {
      double cx, cy;
      int top, bottom;  // rows its disc or ring can touch
    };

    // The inclusive columns a row's chord of radius-`r` disc can touch, one pixel wider each
    // side than the exact chord, so the per-pixel tests decide exactly as a full box scan would.
    bool chord(double cx, double dy, double r, int w, int& x0, int& x1) {
      const double q = r * r - dy * dy;
      if (!(q > 0.0)) return false;
      const double half = std::sqrt(q);
      x0 = clampToInt(std::floor(cx - half - 0.5) - 1.0, 0, w - 1);
      x1 = clampToInt(std::ceil(cx + half - 0.5) + 1.0, 0, w - 1);
      return true;
    }

    // One row of the pass. `runs` counts, per column, the long interior runs laid over it so far
    // (a Fenwick tree: range add, point query); `done[x]` how many of them column x has taken.
    struct Row {
      std::uint8_t* buf;
      int w, h, y = 0;
      Rgba fill;
      std::vector<int> runs, done;
      std::vector<std::pair<int, int>> lazy;
      std::size_t work = 0;
      // powers[j][ch][v]: channel ch after 2^j full-coverage fill blends (blendPixel's arithmetic).
      // A blend moves a channel toward the fill or leaves it for good, so 255 of them are final.
      std::array<std::array<std::array<std::uint8_t, 256>, 4>, 8> powers{};

      Row(std::uint8_t* buf, int w, int h, const Rgba& fill) : buf(buf), w(w), h(h), fill(fill) {}

      void cover(int a, int b, int v) {
        for (int i = a + 1; i <= w + 1; i += i & -i) runs[i] += v;
        for (int i = b + 2; i <= w + 1; i += i & -i) runs[i] -= v;
      }
      int covering(int x) const {
        int n = 0;
        for (int i = x + 1; i > 0; i -= i & -i) n += runs[i];
        return n;
      }
      void layRun(int a, int b) {
        if (runs.empty()) {
          runs.assign(static_cast<std::size_t>(w) + 2, 0);
          done.assign(static_cast<std::size_t>(w), 0);
          const int to[4] = {fill.r, fill.g, fill.b, CHANNEL_MAX};
          for (int ch = 0; ch < 4; ++ch)
            for (int v = 0; v < 256; ++v)
              powers[0][ch][v] = blend::div255(to[ch] * fill.a + v * (CHANNEL_MAX - fill.a));
          for (std::size_t j = 1; j < powers.size(); ++j)
            for (int ch = 0; ch < 4; ++ch)
              for (int v = 0; v < 256; ++v) powers[j][ch][v] = powers[j - 1][ch][powers[j - 1][ch][v]];
        }
        cover(a, b, 1);
        lazy.emplace_back(a, b);
        ++work;
      }

      // Column x takes the interior blends it owes before anything later lands on it.
      void settle(int x) {
        if (lazy.empty()) return;
        const int owed = covering(x) - done[x];
        if (owed <= 0) return;
        done[x] += owed;
        const int m = std::min(owed, CHANNEL_MAX);
        std::uint8_t* p = buf + rgbaOffset(x, y, w);
        for (int ch = 0; ch < 4; ++ch)
          for (std::size_t j = 0; j < powers.size(); ++j)
            if ((m >> j) & 1) p[ch] = powers[j][ch][p[ch]];
        ++work;
      }

      void paint(int x, const Rgba& c, double cov) {
        settle(x);
        blendPixel(buf, w, h, x, y, c, cov);
      }

      void finish() {
        if (lazy.empty()) return;
        std::sort(lazy.begin(), lazy.end());
        int upTo = -1;
        for (const auto& [a, b] : lazy) {
          for (int x = std::max(a, upTo + 1); x <= b; ++x, ++work) {
            settle(x);
            done[x] = 0;
          }
          upTo = std::max(upTo, b);
        }
        for (const auto& [a, b] : lazy) cover(a, b, -1);
        lazy.clear();
      }
    };

    // The disc: coverage 1 within radius - 0.5 of the centre, radius + 0.5 - d in the 1 px rim.
    // Its full-coverage columns are one run; the chord gives it and the exact test settles its ends.
    void discRow(Row& row, const Marker& m, double radius) {
      const double dy = (row.y + 0.5) - m.cy;
      const double rIn = radius - 0.5, rInSq = rIn > 0.0 ? rIn * rIn : -1.0;
      const double rOut = radius + 0.5, rOutSq = rOut * rOut;
      int x0 = 0, x1 = -1;
      if (!chord(m.cx, dy, rOut, row.w, x0, x1)) return;
      const auto inside = [&](int x) {
        const double dx = (x + 0.5) - m.cx;
        return dx * dx + dy * dy <= rInSq;
      };
      int s0 = x1 + 1, s1 = x1;
      if (rInSq - dy * dy >= 0.0) {
        const double half = std::sqrt(rInSq - dy * dy);
        s0 = clampToInt(std::ceil(m.cx - half - 0.5), x0, x1 + 1);
        s1 = clampToInt(std::floor(m.cx + half - 0.5), x0 - 1, x1);
        while (s0 > x0 && inside(s0 - 1)) --s0;
        while (s0 <= s1 && !inside(s0)) ++s0;
        while (s1 < x1 && inside(s1 + 1)) ++s1;
        while (s1 >= s0 && !inside(s1)) --s1;
      }
      for (int x = x0; x <= x1; ++x) {
        if (x == s0 && s0 <= s1) {
          x = s1;
          continue;
        }
        ++row.work;
        const double dx = (x + 0.5) - m.cx;
        const double dsq = dx * dx + dy * dy;
        if (dsq >= rOutSq) continue;
        const double cov = radius + 0.5 - std::sqrt(dsq);
        if (cov > 0.0) row.paint(x, row.fill, cov);
      }
      if (s0 > s1) return;
      if (s1 - s0 < DIRECT_RUN) {
        for (int x = s0; x <= s1; ++x, ++row.work) row.paint(x, row.fill, 1.0);
      } else {
        row.layRun(s0, s1);
      }
    }

    // The 1 px ring: coverage is non-zero only in the band [radius - 1, radius + 1].
    void ringRow(Row& row, const Marker& m, double radius, const Rgba& c) {
      const double dy = (row.y + 0.5) - m.cy;
      const double half = 1.0;
      const double bandOut = radius + half, bandOutSq = bandOut * bandOut;
      const double bandIn = radius - half, bandInSq = bandIn > 0.0 ? bandIn * bandIn : -1.0;
      int x0 = 0, x1 = -1, s0 = 0, s1 = -1;
      if (!chord(m.cx, dy, bandOut, row.w, x0, x1)) return;
      // The hole: columns strictly inside the inner chord, whose pixels all fail the band test.
      if (bandIn > 0.0 && bandInSq - dy * dy > 0.0) {
        const double hi = std::sqrt(bandInSq - dy * dy);
        s0 = clampToInt(std::ceil(m.cx - hi - 0.5) + 1.0, x0, x1 + 1);
        s1 = clampToInt(std::floor(m.cx + hi - 0.5) - 1.0, x0 - 1, x1);
      }
      for (int x = x0; x <= x1; ++x) {
        if (x == s0 && s0 <= s1) {
          x = s1;
          continue;
        }
        ++row.work;
        const double dx = (x + 0.5) - m.cx;
        const double dsq = dx * dx + dy * dy;
        if (dsq >= bandOutSq || dsq <= bandInSq) continue;
        const double cov = std::clamp(half - std::abs(std::sqrt(dsq) - radius), 0.0, 1.0);
        if (cov > 0.0) row.paint(x, c, cov);
      }
    }

  }  // namespace

  std::size_t drawMarkers(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                          double radius, const Rgba& fill, const Rgba& ring) {
    if (w <= 0 || h <= 0 || !(radius > 0.0)) return 0;
    const double reach = radius + 2.0, outer = radius + 0.5 + 1.0;
    std::vector<Marker> ms;
    for (const Point& p : pts) {
      if (p.x + reach < 0.0 || p.y + reach < 0.0 || p.x - reach > w || p.y - reach > h) continue;
      ms.push_back({p.x, p.y, clampToInt(std::floor(p.y - outer), 0, h - 1),
                    clampToInt(std::ceil(p.y + outer), 0, h - 1)});
    }
    if (ms.empty()) return 0;
    std::vector<std::size_t> byTop(ms.size());
    std::iota(byTop.begin(), byTop.end(), std::size_t{0});
    std::stable_sort(byTop.begin(), byTop.end(),
                     [&](std::size_t l, std::size_t r) { return ms[l].top < ms[r].top; });
    int last = 0;
    for (const Marker& m : ms) last = std::max(last, m.bottom);
    Row row(buf, w, h, fill);
    std::vector<std::size_t> active;  // in point order: a pixel takes its markers in that order
    std::size_t next = 0;
    for (int y = ms[byTop[0]].top; y <= last; ++y) {
      if (active.empty() && next < byTop.size()) y = std::max(y, ms[byTop[next]].top);
      const std::size_t held = active.size();
      while (next < byTop.size() && ms[byTop[next]].top <= y) active.push_back(byTop[next++]);
      std::sort(active.begin() + static_cast<std::ptrdiff_t>(held), active.end());
      std::inplace_merge(active.begin(), active.begin() + static_cast<std::ptrdiff_t>(held), active.end());
      active.erase(std::remove_if(active.begin(), active.end(),
                                  [&](std::size_t i) { return ms[i].bottom < y; }),
                   active.end());
      row.y = y;
      for (const std::size_t i : active) {
        discRow(row, ms[i], radius);
        ringRow(row, ms[i], radius, ring);
      }
      row.finish();
    }
    return row.work;
  }

}  // namespace stencil::core::markers
