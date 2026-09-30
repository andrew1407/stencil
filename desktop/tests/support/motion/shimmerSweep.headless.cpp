// Headless checks for the row shimmer's band (support/motion/ShimmerOverlay.hpp shimmerSweepSpan,
// the browser's ::after ui-shimmer sweep): off the row at both ends, crossing it on the way, and
// a sweep rather than a whole-row flash.
#include "../../../src/support/motion/ShimmerOverlay.hpp"

#include <QCoreApplication>

#include "../check.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using stencil::gui::shimmerSweepSpan;
  const QRectF row(0, 0, 300, 24);
  // The lit part is the gradient's 0.38..0.62 stretch.
  const auto lit = [](const stencil::gui::ShimmerSpan& s) {
    const qreal w = s.x1 - s.x0;
    return std::pair{s.x0 + 0.38 * w, s.x0 + 0.62 * w};
  };

  const auto [startL, startR] = lit(shimmerSweepSpan(row, 0.0));
  check(startR < row.left(), "the band starts fully off the row's left edge");
  const auto [endL, endR] = lit(shimmerSweepSpan(row, 1.0));
  check(endL > row.right(), "the band ends fully past the row's right edge");
  const auto [midL, midR] = lit(shimmerSweepSpan(row, 0.5));
  check(midL > row.left() && midR < row.right(), "half-way the lit band is inside the row");
  check(midR - midL < row.width() * 0.3, "the lit band is a narrow sweep, never the whole row");
  const auto [q1L, q1R] = lit(shimmerSweepSpan(row, 0.4));
  const auto [q3L, q3R] = lit(shimmerSweepSpan(row, 0.6));
  check(q1L < midL && midL < q3L, "the band travels left to right as t grows");
  const auto shifted = shimmerSweepSpan(row.translated(40, 0), 0.5);
  check(qFuzzyCompare(shifted.x0, shimmerSweepSpan(row, 0.5).x0 + 40), "the band follows the row's left edge");
  check(stencil::gui::SHIMMER_ROW_MS == 750, "rows sweep on the browser's 0.75 s clock");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
