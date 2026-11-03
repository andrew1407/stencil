// Headless checks for support/menu/popupSlide.hpp (browser twin: overlays.css menuFromAnchor with
// dropdownMenu.js's origin): the slide's first frame is the list at POPUP_SLIDE_SCALE around its origin, that
// origin clamped onto the edge nearest the combo, a speck sitting on it, 6px up; and
// Transition's exit, a click-through ghost of the closed list that shrinks away and removes itself.
#include "../../../src/support/menu/popupSlide.hpp"
#include "../../../src/support/motionPrefs.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <cstdio>
#include <cstdlib>

#include "../check.hpp"

using namespace stencil::support;


int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const QRect list(100, 200, 150, 300);

  // A list below its combo: the caret sits above it, so the origin lands on the top edge.
  const QPoint caret(236, 190);
  const QRect below = slideStartRect(list, caret);
  check(below.width() == 8 && below.height() == 15, "the first frame is a speck: the list at POPUP_SLIDE_SCALE");
  check(below.top() == list.top() - POPUP_SLIDE_LIFT, "…hung from the list's top edge, 6px up");
  check(below.left() <= 236 && below.right() >= 236, "…sitting on the caret, which it grows out of");

  // Flipped above the combo: the caret is below it, so the origin is its bottom edge.
  const QRect above = slideStartRect(list, QPoint(236, 520));
  check(above.bottom() == list.bottom() - POPUP_SLIDE_LIFT, "a list above grows up from its bottom edge");
  check(above.left() <= 236 && above.right() >= 236, "…the speck on the caret there too");

  // An origin inside the list: the speck sits on it.
  const QRect inside = slideStartRect(list, QPoint(160, 320));
  check(inside.contains(QPoint(160, 320 - POPUP_SLIDE_LIFT)), "an origin inside the list is where the speck sits");

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

  {
    QWidget popup(nullptr, Qt::Popup);
    popup.setGeometry(100, 200, 150, 120);
    popup.show();
    const auto ghosts = [] {
      int n = 0;
      for (QWidget* w : QApplication::topLevelWidgets())
        if (qobject_cast<QLabel*>(w) && w->isVisible() && w->testAttribute(Qt::WA_TransparentForMouseEvents) == false
            && (w->windowFlags() & Qt::WindowTransparentForInput)) ++n;
      return n;
    };
    check(shrinkPopupOut(popup, QPoint(175, 195)), "a shown list shrinks away");
    popup.hide();
    check(ghosts() == 1, "…as a click-through window of its own");
    QElapsedTimer t;
    t.start();
    while (ghosts() > 0 && t.elapsed() < 2000) QApplication::processEvents(QEventLoop::AllEvents, 20);
    check(ghosts() == 0, "…which removes itself when the run ends");
  }

  std::printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
