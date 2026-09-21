// Headless check of the modal-popover placement (src/support/popover.hpp) — the desktop
// port of browser/js/ui/tip/popover.js popoverPosition, kept rule-for-rule with the browser's
// tests/popover.test.js placement cases: below the anchor left-aligned, flipped above on
// bottom overflow, clamped inside the screen on both axes. Pure QtCore; no display needed.
#include "popover.hpp"

#include <QCoreApplication>
#include <cstdio>

using stencil::support::popoverRect;

#include "../../support/check.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QRect screen(0, 0, 1280, 800);
  const QSize box(420, 300);

  {
    // QRect(x, y, w, h): an anchor icon at (100, 20), 24px tall → bottom edge at y=44.
    const QRect r = popoverRect(QRect(100, 20, 24, 24), box, screen);
    check(r.left() == 100 && r.top() == 52, "below the anchor, left-aligned, with the gap");
  }
  {
    const QRect r = popoverRect(QRect(100, 700, 24, 24), box, screen);
    check(r.top() == 700 - 8 - 300, "flips above when the bottom would overflow");
  }
  {
    const QRect tall = popoverRect(QRect(100, 300, 24, 24), QSize(420, 700), QRect(0, 0, 1280, 760));
    check(tall.top() == 760 - 8 - 700, "a box taller than either side pins to the bottom margin");
    const QRect huge = popoverRect(QRect(100, 300, 24, 24), QSize(420, 900), QRect(0, 0, 1280, 760));
    check(huge.top() == 8, "…and never above the top margin");
  }
  {
    const QRect right = popoverRect(QRect(1200, 20, 24, 24), box, screen);
    check(right.left() == 1280 - 8 - 420, "clamps at the right edge");
    const QRect left = popoverRect(QRect(2, 20, 24, 24), box, screen);
    check(left.left() == 8, "clamps at the left edge");
  }
  {
    // A screen that does not start at (0,0) — a second monitor — clamps in ITS frame.
    const QRect second(1440, 100, 1280, 800);
    const QRect r = popoverRect(QRect(1440, 100, 24, 24), box, second);
    check(r.left() == 1448 && r.top() >= second.top() + 8, "clamps within an offset screen");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
