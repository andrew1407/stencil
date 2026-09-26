// Headless checks for support/menu/popupSlide.hpp (browser twin: overlays.css menuFromAnchor with
// dropdownMenu.js's origin): the slide's first frame is the list at 0.66 around its origin, that
// origin clamped onto the edge nearest the combo and held at the same fractional spot, 6px up.
#include "../../../src/support/menu/popupSlide.hpp"
#include "../../../src/support/motionPrefs.hpp"

#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>

#include "../check.hpp"

using namespace stencil::support;

namespace {
  // Where `p` sits across `r`, 0 at the left/top pixel and 1 at the right/bottom one.
  double fracX(const QRect& r, int x) { return double(x - r.left()) / qMax(r.width() - 1, 1); }
  double fracY(const QRect& r, int y) { return double(y - r.top()) / qMax(r.height() - 1, 1); }
  bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const QRect list(100, 200, 150, 300);

  // A list below its combo: the caret sits above it, so the origin lands on the top edge.
  const QPoint caret(236, 190);
  const QRect below = slideStartRect(list, caret);
  check(below.width() == 99 && below.height() == 198, "the first frame is the list at 0.66");
  check(below.top() == list.top() - POPUP_SLIDE_LIFT, "…hung from the list's top edge, 6px up");
  check(near(fracX(below, 236), fracX(list, 236), 0.02),
        "…with the caret at the same fractional spot across it");

  // Flipped above the combo: the caret is below it, so the origin is its bottom edge.
  const QRect above = slideStartRect(list, QPoint(236, 520));
  check(above.bottom() == list.bottom() - POPUP_SLIDE_LIFT, "a list above grows up from its bottom edge");
  check(near(fracX(above, 236), fracX(list, 236), 0.02), "…the caret held at its spot there too");

  // An origin inside the list keeps its spot on both axes.
  const QPoint mid(160, 320);
  const QRect inside = slideStartRect(list, mid);
  check(near(fracX(inside, 160), fracX(list, 160), 0.02) &&
            near(fracY(inside, 320 - POPUP_SLIDE_LIFT), fracY(list, 320), 0.02),
        "an origin inside the list stays where it is in the shrunken frame");

  // Off to one side: clamped onto the nearest corner.
  const QRect left = slideStartRect(list, QPoint(20, 150));
  check(left.topLeft() == QPoint(list.left(), list.top() - POPUP_SLIDE_LIFT),
        "an origin past the corner pins that corner");
  check(!slideStartRect(QRect(), caret).isValid(), "an unsized popup gets no first frame");

  // The runner stays off where the gui tests run, like the dust.
  setMotionMode(MotionMode::SLIDE);
  const bool offscreen = QGuiApplication::platformName() == QLatin1String("offscreen");
  check(!offscreen || !isSlideMotionOk(), "no slide on the offscreen platform");
  setMotionMode(MotionMode::PARTICLES);
  check(!isSlideMotionOk(), "no slide outside the 'slide' mode");

  std::printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
