// Opening a dialog at a point (support/modal/modalReveal.hpp DialogLanding): the next dialog to show
// opens with its frame's top-left on the point, shifted left or up only as far as its host's screen
// needs, and grows out of the cursor there; a later dialog, and one shown after a landing no dialog
// claimed, is placed as before. topLeftAt and centredTopLeft are the box maths.
#include "modalReveal.hpp"
#include "../../MainWindowFlight.gui.hpp"   // RevealOriginWatcher: where a flight forms

#include <QApplication>
#include <QDialog>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include "../check.hpp"

using namespace stencil::support;

namespace {
  // Shown modally; its frame is read once it is up, then it closes.
  QRect frameWhenShown(QDialog& dlg) {
    QRect frame;
    QTimer::singleShot(0, &dlg, [&frame, &dlg] {
      frame = dlg.frameGeometry();
      dlg.reject();
    });
    dlg.exec();
    return frame;
  }

  bool near(const QPoint& a, const QPoint& b) { return (a - b).manhattanLength() <= 2; }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  installDialogCentring();

  const QRect avail(0, 0, 800, 600);
  const QSize box(200, 100);
  check(topLeftAt(QPoint(300, 250), box, avail) == QPoint(300, 250), "a box that fits keeps its top-left on the point");
  check(topLeftAt(QPoint(700, 560), box, avail) == QPoint(600, 500),
        "…one reaching past the right or bottom edge shifts left and up to fit");
  check(topLeftAt(QPoint(-20, -5), box, avail) == QPoint(0, 0), "…and one starting off the top-left comes back on");
  check(topLeftAt(QPoint(700, 560), box, QRect()) == QPoint(700, 560), "with no screen it is as given");
  check(centredTopLeft(QPoint(400, 300), box, avail) == QPoint(300, 250) &&
            centredTopLeft(QPoint(790, 590), box, avail) == QPoint(600, 500),
        "a plain centring still centres, kept on the screen alike");
  check(cursorOrigin(QPoint(100, 100)) == QRect(88, 88, 24, 24), "the cursor's origin is 24 px on the point");

  QWidget host;
  host.setGeometry(100, 100, 500, 400);
  host.show();
  const QRect screen = host.screen()->availableGeometry();
  const QPoint at(180, 160);   // well off the host's centre

  QRect first, second;
  {
    const DialogLanding landing(at);
    QDialog a(&host), b(&host);
    a.resize(160, 120);
    b.resize(160, 120);
    first = frameWhenShown(a);
    second = frameWhenShown(b);
  }
  check(first.topLeft() == at, "the next dialog to show opens with its frame's top-left on the point");
  check(second.topLeft() != at, "…and the one after it does not");

  {
    const DialogLanding landing(screen.bottomRight() - QPoint(20, 20));
    QDialog d(&host);
    d.resize(160, 120);
    const QRect f = frameWhenShown(d);
    check(screen.contains(f) && f.right() == screen.right() && f.bottom() == screen.bottom(),
          "a landing near the screen's corner shifts left and up just enough to fit");
  }

  { const DialogLanding unclaimed(at); }
  QDialog later(&host);
  later.resize(160, 120);
  check(frameWhenShown(later).topLeft() != at, "a landing no dialog claimed ends with its scope");

  // With motion on, the opening flight forms out of the cursor, and a plain open out of its opener.
  qunsetenv("STENCIL_NO_ANIM");
  if (!motionReduced()) {
    QWidget opener(&host);
    opener.setGeometry(10, 10, 28, 28);
    opener.show();
    const auto originOf = [&](bool landed) {
      stencil::guitest::RevealOriginWatcher watch;
      app.installEventFilter(&watch);
      QDialog flown(&host);
      flown.resize(160, 120);
      revealDialog(flown, &opener);
      if (landed) {
        const DialogLanding landing(at);
        frameWhenShown(flown);
      } else {
        frameWhenShown(flown);
      }
      app.removeEventFilter(&watch);
      stencil::guitest::awaitFlights(&host);
      return watch.captured ? watch.origin : QPoint(-1, -1);
    };
    check(near(originOf(true), host.mapFromGlobal(at)), "a landed dialog grows out of the cursor");
    check(near(originOf(false), opener.geometry().center()), "…and a plain one out of its opener");
  }

  return failures ? 1 : 0;
}
