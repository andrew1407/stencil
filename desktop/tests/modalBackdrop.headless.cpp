// The window backdrop (support/ModalBackdrop): the browser's `.app-modal-overlay` scrim +
// `backdrop-filter: blur()`, which Qt has no equivalent for, so the page behind is
// photographed once and blurred.
//   - the Visuals switch gates it: off means nothing is created at all;
//   - blurred() really softens the picture, and keeps its size and device pixel ratio;
//   - it covers the host, paints under a scrim, and takes no clicks;
//   - it goes when the dialog that asked for it closes, however it was dismissed.
#include "ModalBackdrop.hpp"
#include "motionPrefs.hpp"

#include <QApplication>
#include <QDialog>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <cmath>
#include <cstdio>

using namespace stencil;
using support::ModalBackdrop;

#include "support/check.hpp"

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
    // Dismissal, whichever way, takes it with the dialog: finished() is wired to
    // deleteLater, so after the loop drains the deferred deletes it is gone from the host.
    QPointer<QWidget> guard(bd);
    dlg.reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(guard.isNull(), "behind: closing the dialog takes the backdrop with it");
    check(host.findChild<QWidget*>("modalBackdrop") == nullptr, "behind: the host is clean again");
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
