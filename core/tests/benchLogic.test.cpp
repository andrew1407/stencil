// Parse / state / colour half of the opt-in `bench` suite — design: bench.test.cpp.
#include "doctest.h"

#include "benchSupport.hpp"
#include "colorNames.hpp"
#include "formulaParser.hpp"
#include "luma.hpp"
#include "ProjectsStore.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace stencil::core;
using namespace bench;

namespace {

  // `d` nested parens: 2 DepthGuards per level, so 127 is the deepest accepted (255/256).
  std::string nested(int d) {
    return std::string(static_cast<std::size_t>(d), '(') + "x" +
           std::string(static_cast<std::size_t>(d), ')');
  }

  // A flat expression of the same token count: "x+x+x+..." with `terms` variables.
  std::string flat(int terms) {
    std::string s = "x";
    for (int i = 1; i < terms; ++i) s += "+x";
    return s;
  }

  std::vector<ProjectMeta> registry(int n, long long now, bool halfExpired) {
    std::vector<ProjectMeta> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      ProjectMeta m;
      m.id = "p_" + std::to_string(i);
      m.name = "Project " + std::to_string(i);
      m.createdAt = now - i;
      m.updatedAt = now - (i * 7) % n;
      m.expiresAt = (halfExpired && (i % 2 == 0)) ? now - 1000 : now + ProjectsStore::EXPIRY_MS;
      m.description = "a saved annotation project";
      m.keywords = {"alpha", "beta"};
      out.push_back(m);
    }
    return out;
  }

}  // namespace

TEST_SUITE("bench") {

  // ── Deeply nested formulas ─────────────────────────────────────────────────
  // MAX_DEPTH = 256 bounds recursion, not its COST — and formulas arrive from untrusted
  // layout JSON, the console and --formula. Against a flat expression of the same token
  // count, depth must cost only a constant factor over width.
  TEST_CASE("bench: formulaParser deep nesting stays bounded" * doctest::skip()) {
    const int deepest = 127;                     // last accepted nesting
    const std::string ok = nested(deepest);      // 255 guards deep
    const std::string over = nested(deepest * 8);  // rejected at the cap
    const std::string wide = flat(deepest + 1);  // same token count, depth 2

    REQUIRE(FormulaParser::validate(ok, 'x'));
    REQUIRE_FALSE(FormulaParser::validate(over, 'x'));

    const int reps = 2000;
    auto run = [&](const std::string& e) {
      return best_ms(3, [&] {
        double acc = 0;
        for (int i = 0; i < reps; ++i) acc += FormulaParser::apply(e, 'x', 1.5, true);
        volatile double sink = acc;
        (void)sink;
      });
    };
    const double tDeep = run(ok);
    const double tOver = run(over);
    const double tWide = run(wide);

    MESSAGE("formula depth " << deepest << " (255 guards) x" << reps << " = " << tDeep
                             << "ms (" << tDeep / reps * 1000.0 << " us each)  flat same-size="
                             << tWide << "ms ratio=" << tDeep / tWide << "  over-cap 8x deeper="
                             << tOver << "ms");
    CHECK(tWide > 0.0);
    CHECK(tDeep < tWide * 20.0);  // depth is linear like width; 20x = generous ceiling
    CHECK(tOver < tDeep * 20.0);  // the cap must ABORT, not walk the whole input
  }

  // ── Projects registry at scale ─────────────────────────────────────────────
  // list() deep-copies every field of every project, on each projects-dialog refresh
  // plus a periodic re-list timer; listRefs() is the no-copy twin, so the ratio is the
  // price of the copy. sweepExpired is the same walk plus one remove() per expired id.
  TEST_CASE("bench: projectsStore list / sweepExpired at N projects" * doctest::skip()) {
    const long long now = 1'700'000'000'000LL;
    const int n = 2000;
    ProjectsStore s1, s2;
    s1.load(registry(n, now, false));
    s2.load(registry(n * 2, now, false));

    volatile std::size_t sink = 0;
    const double t1 = best_ms(5, [&] { sink += s1.list().size(); });
    const double t2 = best_ms(5, [&] { sink += s2.list().size(); });
    const double tRefs = best_ms(5, [&] { sink += s1.listRefs().size(); });

    // Re-load per rep so the sweep runs unswept; load-only is the baseline to subtract.
    const auto reg1 = registry(n, now, true);
    const auto reg2 = registry(n * 2, now, true);
    const double load1 = best_ms(3, [&] { ProjectsStore s; s.load(reg1); sink += s.registry().size(); });
    const double sweep1 = best_ms(3, [&] { ProjectsStore s; s.load(reg1); sink += s.sweepExpired(now).size(); });
    const double sweep2 = best_ms(3, [&] { ProjectsStore s; s.load(reg2); sink += s.sweepExpired(now).size(); });

    MESSAGE("projects N=" << n << " list=" << t1 << "ms (" << n / t1 / 1e3 << " M rows/s)  2N list=" << t2
                          << "ms ratio=" << t2 / t1 << "  listRefs=" << tRefs << "ms ("
                          << t1 / tRefs << "x cheaper)  sweep=" << sweep1 - load1
                          << "ms over load=" << load1 << "ms  2N sweep=" << sweep2
                          << "ms ratio=" << sweep2 / sweep1);
    CHECK(t1 > 0.0);
    CHECK(t2 < t1 * 3.0);          // list is linear in N
    // The sweep is NOT: remove() erases + reindexes per id, so it measures ~4x per
    // doubling today. Ceiling set to catch a worsening; tighten to 3.0 once it is linear.
    CHECK(sweep2 < sweep1 * 6.0);
    CHECK(tRefs < t1 * 1.5);       // the no-copy path must never cost more than the copy
  }

  // ── Colour resolution per line per frame ───────────────────────────────────
  // rasterizeLine calls parseColor up to three times per line (stroke, fill, points)
  // and the desktop repeats that per line on every PAINT, each call building a
  // std::string key. Hex is a nibble decode, a keyword a table lookup — the ratio says
  // whether a resolved-colour cache would be worth its invalidation cost.
  TEST_CASE("bench: colorNames parseColor hex vs keyword" * doctest::skip()) {
    const std::vector<std::string> hex = {"#3366ff", "#abc", "#11223344", "#FFFF00"};
    const std::vector<std::string> words = {"red", "cornflowerblue", "transparent", "rebeccapurple"};
    const int reps = 40000;
    auto run = [&](const std::vector<std::string>& specs) {
      return best_ms(3, [&] {
        int acc = 0;
        for (int i = 0; i < reps; ++i) {
          const auto c = parseColor(specs[i % specs.size()]);
          acc += c ? c->r : 0;
        }
        volatile int sink = acc;
        (void)sink;
      });
    };
    const double tHex = run(hex);
    const double tWord = run(words);

    MESSAGE("parseColor x" << reps << "  hex=" << tHex << "ms (" << tHex / reps * 1e6
                           << " ns each)  keyword=" << tWord << "ms (" << tWord / reps * 1e6
                           << " ns each)  ratio=" << tWord / tHex << "  table of "
                           << colorNameCount() << " names");
    CHECK(tHex > 0.0);
    CHECK(tWord < tHex * 25.0);  // a linear scan of the table would be far worse
  }

  // ── The two luma formulas ──────────────────────────────────────────────────
  // luma.hpp keeps a float form (filters) and an integer form (contour Sobel) that must
  // never be merged. These numbers settle any attempt to unify them: the cost of each,
  // and how rarely they disagree — 835 of all 16.7M RGB triples (0.005%), never by > 1.
  TEST_CASE("bench: luma float vs integer Rec.709" * doctest::skip()) {
    const auto px = gradient(1400, 1400);  // ~2 M pixels of RGBA
    const std::size_t n = px.size() / 4;

    volatile std::uint64_t sink = 0;
    const double tFloat = best_ms(3, [&] {
      std::uint64_t s = 0;
      for (std::size_t i = 0; i < n; ++i)
        s += static_cast<std::uint64_t>(luma::rec709Truncated(px[i * 4], px[i * 4 + 1], px[i * 4 + 2]));
      sink += s;
    });
    const double tInt = best_ms(3, [&] {
      std::uint64_t s = 0;
      for (std::size_t i = 0; i < n; ++i)
        s += luma::rec709Scaled(px[i * 4], px[i * 4 + 1], px[i * 4 + 2]);
      sink += s;
    });

    int maxDiff = 0, differing = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const int a = luma::rec709Truncated(px[i * 4], px[i * 4 + 1], px[i * 4 + 2]);
      const int b = luma::rec709Scaled(px[i * 4], px[i * 4 + 1], px[i * 4 + 2]);
      const int d = a > b ? a - b : b - a;
      if (d > 0) ++differing;
      if (d > maxDiff) maxDiff = d;
    }

    MESSAGE("luma over " << n / 1e6 << " M px  float=" << tFloat << "ms (" << n / tFloat / 1e3
                         << " M px/s)  integer=" << tInt << "ms (" << n / tInt / 1e3
                         << " M px/s)  ratio=" << tFloat / tInt << "  disagree on " << differing
                         << " px, max delta=" << maxDiff);
    CHECK(tFloat > 0.0);
    // Deterministic invariant behind the split: near-identical, but NOT equal.
    CHECK(maxDiff <= 1);
    CHECK(tFloat < tInt * 8.0);  // the float form is the hot filter path; keep it close
  }

}  // TEST_SUITE("bench")
