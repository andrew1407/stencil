// The desktop's line union (model/lineUnion over core lineMerge): the peer's lines first, each
// local line no earlier one matches after, and whether the peer brought anything. The dedupe key
// is exact, so two lines a 6-significant-digit key would have merged stay two; a join past the
// layout caps is cut as one layout.
#include "lineUnion.hpp"

#include <cstdio>

#include "../support/check.hpp"

namespace core = stencil::core;

namespace {
  core::Line lineAt(double x) {
    core::Line l;
    l.points = {{x, 1}, {x + 10, 2}};
    return l;
  }
}  // namespace

int main() {
  const core::Line a = lineAt(1), b = lineAt(2), c = lineAt(3);
  const auto both = stencil::model::unionLines({a, b}, {b, c});
  check(both.lines.size() == 3 && both.lines[0].points[0].x == 1 && both.lines[1].points[0].x == 2 &&
            both.lines[2].points[0].x == 3,
        "the peer's lines first, then the local one it lacks");
  check(both.peerAdded, "…and the peer brought a line this side did not have");

  const auto mine = stencil::model::unionLines({a}, {a, c});
  check(mine.lines.size() == 2 && !mine.peerAdded, "a peer holding only our lines adds nothing");

  const core::Line near1 = lineAt(123456.1), near2 = lineAt(123456.2);
  const auto close = stencil::model::unionLines({near1}, {near2});
  check(close.lines.size() == 2 && close.peerAdded, "lines apart in the 7th digit stay two");

  core::Line styled = a;
  styled.color = "#FF0000";
  check(stencil::model::unionLines({styled}, {a}).lines.size() == 2, "a different colour is a different line");
  check(stencil::model::unionLines({}, {}).lines.empty(), "nothing and nothing is nothing");

  // Two capped halves joined pass the total: the join is cut as one layout (browser capLayoutPoints).
  core::Line big;
  big.points.assign(100000, core::Point{1, 1});
  const core::Lines half(6, big);
  core::Lines joined = half;
  joined.insert(joined.end(), half.begin(), half.end());
  const core::Lines cut = stencil::model::capLayout(joined);
  std::size_t total = 0;
  for (const auto& l : cut) total += l.points.size();
  check(cut.size() == 10 && total == 1000000, "a combine past the cap stops at the line that spends it");
  joined[9].points.resize(99990);
  const core::Lines crossing = stencil::model::capLayout(joined);
  check(crossing.size() == 11 && crossing[10].points.size() == 10, "the crossing line keeps what the budget allows");
  check(stencil::model::capLayout({a, b}).size() == 2, "under the caps every line stays");
  core::Lines ours;
  for (int i = 0; i < 6; ++i) {
    ours.push_back(big);
    ours.back().points[0].x = 2 + i;
  }
  check(stencil::model::unionLines({big}, ours).lines.size() == 7, "six lines of our own join the peer's one");
  check(stencil::model::unionLines(half, ours).lines.size() == 10, "a co-edit union past the cap is cut too");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
