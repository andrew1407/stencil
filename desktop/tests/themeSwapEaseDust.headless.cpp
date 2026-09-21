// The grain's own curve against the browser's own answers (surface/motion.js swapDustEase / swapDustFrame),
// and the rule that no mote is ever handed back with nothing to show.
#include "themeSwapEaseParts.hpp"

void dustGrainCurve(double w, double h) {
  {
    struct Row { double life, ease, x, r, alpha; };
    static constexpr Row BROWSER[] = {
      {0.000000000, 0.000000000, 0.000000000, 3.000000000, 0.000000000},
      {0.050000000, 0.130636662, 13.063666224, 2.725663009, 0.643748641},
      {0.180000000, 0.456230521, 45.623052120, 2.041915905, 1.000000000},
      {0.250000000, 0.597751975, 59.775197506, 1.744720852, 0.776672065},
      {0.400000000, 0.796361804, 79.636180401, 1.327640212, 0.368897378},
      {0.500000000, 0.877435088, 87.743508816, 1.157386315, 0.211374760},
      {0.750000000, 0.976566136, 97.656613588, 0.949211115, 0.036658227},
      {0.900000000, 0.996591210, 99.659121037, 0.907158458, 0.004921913},
      {0.990000000, 0.999957621, 99.995762110, 0.900088996, 0.000042379},
      {1.000000000, 1.000000000, 100.000000000, 0.900000000, 0.000000000},
    };
    // The browser tabulates its curve in float32, this one in double: they agree to far
    // more than a grain (or an 8-bit alpha) can show.
    constexpr double EPS = 1e-5;
    bool aligned = true;
    for (const Row& r : BROWSER) {
      const double e = ThemeSwapOverlay::grainEase(r.life);
      const double o = r.life < ThemeSwapOverlay::GRAIN_FLARE
                           ? ThemeSwapOverlay::grainEase(r.life / ThemeSwapOverlay::GRAIN_FLARE)
                           : 1.0 - ThemeSwapOverlay::grainEase(
                                       (r.life - ThemeSwapOverlay::GRAIN_FLARE)
                                       / (1.0 - ThemeSwapOverlay::GRAIN_FLARE));
      aligned = aligned && std::abs(e - r.ease) < EPS
                && std::abs(100.0 * e - r.x) < EPS            // the whole throw, eased
                && std::abs(3.0 * (1 - 0.7 * e) - r.r) < EPS  // scale(0.3) by the end
                && std::abs(o - r.alpha) < EPS;
    }
    check(aligned, "the grain rides the browser's curve, not an approximation of it");
    // …and the WAKE rides that curve, not just the helper: take a real mote at each of
    // those lives and read the ease back out of the size and the alpha it came with.
    {
      const QPointF org(90, 60);
      const double full3 = fullRadius(org.x(), org.y(), w, h);
      // Most indices are culled (off screen, or behind the ring) — take the first that
      // survives its whole flight, rather than pinning one the noise could move.
      const auto ignition = [](int i) {
        return ThemeSwapOverlay::DUST_MIN_T
               + ThemeSwapOverlay::dustNoise(i + 57, 11)
                     * (ThemeSwapOverlay::DUST_MAX_T - ThemeSwapOverlay::DUST_MIN_T);
      };
      const auto at = [&](int i, double life, ThemeSwapOverlay::DustMote* out) {
        return ThemeSwapOverlay::dustMoteAt(
            i, ignition(i) * ThemeSwapOverlay::SWAP_MS + life * ThemeSwapOverlay::DUST_LIFE_MS,
            org, full3, QSizeF(w, h), out);
      };
      int i = -1;
      ThemeSwapOverlay::DustMote mo{};
      for (int c = 0; c < ThemeSwapOverlay::DUST_MOTES && i < 0; c++) {
        bool whole = true;
        for (const Row& r : BROWSER)
          if (r.life > 0 && r.alpha >= 1.0 / 255) whole = whole && at(c, r.life, &mo);
        if (whole) i = c;
      }
      const double base = 2.5 + ThemeSwapOverlay::dustNoise(i, 3) * 3.5;
      const double lit = 0.75 + ThemeSwapOverlay::dustNoise(i + 13, 29) * 0.25;
      bool rides = i >= 0;
      int sampled = 0;
      for (const Row& r : BROWSER) {
        if (r.life <= 0 || r.alpha < 1.0 / 255) continue;   // not alive, or too faint to paint
        if (!at(i, r.life, &mo)) continue;
        sampled++;
        // Read the ease back out of what the mote came with: size is base * (1 − 0.7e),
        // alpha is its own brightness times the flare.
        rides = rides && std::abs((1.0 - mo.size / base) / 0.7 - r.ease) < 1e-4
                && std::abs(mo.alpha / lit - r.alpha) < 1e-4;
      }
      check(sampled >= 5 && rides, "a mote's own size and alpha carry the browser's curve");
    }
    // Ends exactly: a grain starts home and unlit, and leaves nothing behind.
    check(ThemeSwapOverlay::grainEase(0.0) == 0.0 && ThemeSwapOverlay::grainEase(1.0) == 1.0,
          "both ends of the curve are pinned");
    // An invisible grain is not a grain: below one 8-bit alpha step it declines to paint.
    ThemeSwapOverlay::DustMote faint{};
    const QPointF o2(90, 60);
    const double full2 = fullRadius(o2.x(), o2.y(), w, h);
    bool anyInvisible = false;
    for (double ms = 1; ms <= ThemeSwapOverlay::SWAP_MS + ThemeSwapOverlay::DUST_LIFE_MS; ms += 1)
      for (int i = 0; i < 400; i++)
        if (ThemeSwapOverlay::dustMoteAt(i, ms, o2, full2, QSizeF(w, h), &faint))
          anyInvisible = anyInvisible || faint.alpha < 1.0 / 255;
    check(!anyInvisible, "no grain is handed back with nothing to show");
  }
}
