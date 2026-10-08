// CropPreview's pinned (setFitBox) and elastic (setPreferredBox) fit, dialogs/crop/CropDialog.cpp — an
// invalid or degenerate box must never fall back to scale 1.0 (the image's own NATIVE pixels): for a photo
// or video frame that dwarfs the dialog, that is the "crop covers the whole window" bug. OpenImageDialog's
// inline stage skips the constructor's screen-relative first fit (autoFitScreen=false) for the same reason.
#include "CropDialog.hpp"
#include "cropDialogParts.hpp"
#include "cropGeometry.hpp"

#include <QApplication>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::CropPreview;
using stencil::gui::INSET;

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  QImage frame(2874, 4064, QImage::Format_RGB32);   // a real portrait video frame's size
  frame.fill(Qt::darkGreen);
  stencil::core::CropRect initial;   // empty: centeredCrop() picks it

  {
    // The realistic sequence: skip the auto-fit, then immediately ask for the real box —
    // the box OpenImageDialog::syncCropStage() actually uses.
    CropPreview p(frame, 29.7, 42.0, initial, nullptr, /*autoFitScreen=*/false);
    p.setFitBox(QSize(440, 300));
    check(p.width() <= 470 && p.height() <= 330,
          "a real fit box lands the widget within it, plus the handle inset");
  }
  {
    // A degenerate box (what an uninitialized fitBox reads back as, QSize()'s -1x-1) must
    // be REFUSED, not answered with scale 1.0 — the widget keeps its last good fit.
    CropPreview p(frame, 29.7, 42.0, initial, nullptr, /*autoFitScreen=*/false);
    p.setFitBox(QSize(440, 300));
    const QSize before = p.size();
    p.setFitBox(QSize(-1, -1));
    check(p.size() == before, "an invalid box is a no-op, not a jump to native pixels");
    p.setFitBox(QSize(0, 0));
    check(p.size() == before, "…and so is a zero one");
  }

  {
    // Pinned (the open-image stage): the picture fills the fixed size inside the inset, as it always did.
    CropPreview p(frame, 29.7, 42.0, initial, nullptr, /*autoFitScreen=*/false);
    p.setFitBox(QSize(440, 300));
    check(p.paintedRect() == p.rect().adjusted(INSET, INSET, -INSET, -INSET),
          "a pinned stage paints exactly inside its handle inset");
  }
  {
    // Elastic (the crop editor's stage): the fitted picture is only the hint; any size re-fits it,
    // centred, and a degenerate box is refused like a pinned one.
    QImage wide(3000, 2000, QImage::Format_RGB32);
    CropPreview p(wide, 21.0, 29.7, initial, nullptr, /*autoFitScreen=*/false);
    p.setPreferredBox(QSize(600, 600));
    check(p.sizeHint() == QSize(600 + 2 * INSET, 400 + 2 * INSET),
          "the hint is the picture fitted into the box, plus the inset");
    check(p.minimumWidth() < p.sizeHint().width() && p.maximumWidth() > p.sizeHint().width(),
          "…but the size is the layout's to give");
    p.resize(1000, 300);
    const QRect r = p.paintedRect();
    check(r.height() == 300 - 2 * INSET && std::abs(r.center().x() - 500) <= 1,
          "a wider stage paints the picture as tall as it allows, centred");
    p.setPreferredBox(QSize(0, 0));
    check(p.sizeHint() == QSize(600 + 2 * INSET, 400 + 2 * INSET), "a degenerate box keeps the last good hint");
  }

  {
    // setAlbum on a PRESS: carries the user's own off-centre framing across the flip
    // (swapCropOrientation), never a fresh centeredCrop() default over it.
    QImage wide(2880, 2037, QImage::Format_RGB32);
    stencil::core::CropRect dragged{100, 300, 800, 1200};   // off-centre, not the default
    CropPreview p(wide, 21.0, 29.7, dragged, nullptr, /*autoFitScreen=*/false);
    const auto before = p.cropRect();
    const double cx = before.x + before.width / 2.0, cy = before.y + before.height / 2.0;
    p.setAlbum(!p.getAlbum());
    const auto after = p.cropRect();
    check(after.width != before.width || after.height != before.height,
          "the flip changes the box's own dimensions");
    check(std::abs((after.x + after.width / 2.0) - cx) < 1.0 &&
              std::abs((after.y + after.height / 2.0) - cy) < 1.0,
          "the flip keeps the SAME centre — the user's own framing, not a reset");
  }
  {
    // A same-value setAlbum (CropDialog's own ctor bootstrap, forcing the caller's requested orientation
    // over the image-natural guess) must stay a no-op: swapping a correct rect lands the wrong aspect.
    QImage wide(2880, 2037, QImage::Format_RGB32);
    stencil::core::CropRect empty;
    CropPreview p(wide, 21.0, 29.7, empty, nullptr, /*autoFitScreen=*/false);
    const bool albumBefore = p.getAlbum();
    const auto before = p.cropRect();
    p.setAlbum(albumBefore);
    const auto after = p.cropRect();
    check(after.width == before.width && after.height == before.height,
          "setAlbum(same value) leaves an already-correct rect alone");
  }

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}
