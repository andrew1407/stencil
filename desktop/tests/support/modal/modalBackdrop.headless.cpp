// The window backdrop (support/ModalBackdrop): the browser's `.app-modal-overlay` scrim +
// `backdrop-filter: blur()`, which Qt has no equivalent for, so the page behind is photographed once
// and blurred. The Visuals switch gates it (off creates nothing), blurred() really softens the picture
// and keeps its size and device pixel ratio, it covers the host under a scrim and takes no clicks, and
// it goes when the dialog that asked for it closes, however that dialog was dismissed — fading in
// and out on the browser's overlayFade clocks, or landing whole under reduced motion.
#include "ModalBackdrop.hpp"
#include "motionPrefs.hpp"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <cmath>
#include <cstdio>

using namespace stencil;
using support::ModalBackdrop;

#include "../../support/check.hpp"

// Mean absolute difference between neighbouring pixels along a row: a sharp picture has a
// lot, a blurred one much less.
static double contrast(const QImage& img) {
  double sum = 0;
  int n = 0;
  for (int y = 0; y < img.height(); y += 2)
    for (int x = 1; x < img.width(); ++x) {
      sum += std::abs(qGray(img.pixel(x, y)) - qGray(img.pixel(x - 1, y)));
      ++n;
    }
  return n ? sum / n : 0;
}

// Drive the animation clock for a stretch: this test owns no event loop of its own.
static void pump(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// A hard checkerboard: the most contrast a blur can take away.
static QPixmap checkerboard(int w, int h, int cell) {
  QPixmap pm(w, h);
  QPainter p(&pm);
  for (int y = 0; y < h; y += cell)
    for (int x = 0; x < w; x += cell)
      p.fillRect(x, y, cell, cell, ((x / cell + y / cell) % 2) ? Qt::white : Qt::black);
  return pm;
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // ── blurred(): the picture really softens ──
  {
    const QPixmap sharp = checkerboard(160, 120, 8);
    const QPixmap soft = ModalBackdrop::blurred(sharp, ModalBackdrop::BLUR_PX);
    check(soft.size() == sharp.size(), "blurred: the snapshot keeps its size");
    const double before = contrast(sharp.toImage());
    const double after = contrast(soft.toImage());
    check(after < before / 2, "blurred: neighbouring pixels come much closer together");
    std::printf("      contrast %.1f -> %.1f\n", before, after);
    check(ModalBackdrop::blurred(QPixmap(), 3).isNull(), "blurred: a null shot stays null");
    const QPixmap same = ModalBackdrop::blurred(sharp, 0);
    check(qFuzzyCompare(contrast(same.toImage()) + 1.0, before + 1.0), "blurred: radius 0 is a no-op");
  }

  // ── behind(): gated, placed, and tied to its dialog ──
  QWidget host;
  host.resize(400, 300);
  host.show();
  {
    support::setModalBackdrop(false);
    QDialog dlg(&host);
    check(ModalBackdrop::behind(&dlg, &host) == nullptr,
          "behind: the Visuals switch off creates nothing at all");
  }
  support::setModalBackdrop(true);
  {
    QDialog dlg(&host);
    ModalBackdrop* bd = ModalBackdrop::behind(&dlg, &host);
    check(bd != nullptr, "behind: the switch on gives a backdrop");
    check(bd && bd->parentWidget() == &host, "behind: it lives in the host");
    check(bd && bd->geometry() == host.rect(), "behind: it covers the host");
    check(bd && bd->testAttribute(Qt::WA_TransparentForMouseEvents),
          "behind: it only paints — the dialog owns the input");
    check(bd && bd->isVisible(), "behind: it is up before the dialog runs");
    // The dim and blur come UP, as the browser's overlayFadeIn does, rather than landing whole.
    check(bd && bd->opacityNow() == 0.0, "behind: it starts on the window as it is");
    pump(ModalBackdrop::FADE_IN_MS / 2);
    const double midway = bd->opacityNow();
    check(midway > 0.0 && midway < 1.0, "behind: the scrim comes up over time");
    pump(ModalBackdrop::FADE_IN_MS);
    check(qFuzzyCompare(bd->opacityNow(), 1.0), "behind: …and settles fully dimmed");
    // Dismissal, whichever way, takes it with the dialog — on the way out it fades, so it is
    // still there for those frames and gone once the fade has run.
    QPointer<QWidget> guard(bd);
    dlg.reject();
    pump(ModalBackdrop::FADE_OUT_MS / 3);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(!guard.isNull() && bd->opacityNow() < 1.0, "behind: it does not blink out, it clears");
    pump(ModalBackdrop::FADE_OUT_MS);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(guard.isNull(), "behind: closing the dialog takes the backdrop with it");
    check(host.findChild<QWidget*>("modalBackdrop") == nullptr, "behind: the host is clean again");
  }
  // Reduced motion has no fade at all: up whole, and gone with the dialog.
  {
    const support::MotionMode was = support::motionMode();
    support::setMotionMode(support::MotionMode::NONE);
    QDialog dlg(&host);
    ModalBackdrop* bd = ModalBackdrop::behind(&dlg, &host);
    check(bd && qFuzzyCompare(bd->opacityNow(), 1.0), "reduced motion: the scrim is there at once");
    QPointer<QWidget> guard(bd);
    dlg.reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(guard.isNull(), "reduced motion: and goes at once too");
    support::setMotionMode(was);
  }
  {
    QDialog dlg(&host);
    check(ModalBackdrop::behind(&dlg, nullptr) == nullptr, "behind: no host, no backdrop");
    QWidget tiny;
    tiny.resize(4, 4);
    check(ModalBackdrop::behind(&dlg, &tiny) == nullptr, "behind: nothing worth photographing");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
