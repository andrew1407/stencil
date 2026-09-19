// CropPreview::setFitBox (dialogs/CropDialog.cpp) — an invalid or degenerate box must never fall back
// to scale 1.0 (the image's own NATIVE pixels): for a photo or video frame that dwarfs the dialog,
// that is the "crop covers the whole window" bug. OpenImageDialog's inline stage skips the
// constructor's screen-relative first fit (autoFitScreen=false) for the same reason.
#include "CropDialog.hpp"
#include "cropGeometry.hpp"

#include <QApplication>
#include <cmath>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::CropPreview;

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
    // A degenerate box (what an uninitialized fitBox_ reads back as, QSize()'s -1x-1) must
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
    // setAlbum on a PRESS: carries the user's own off-centre framing across the flip
    // (swapCropOrientation), never a fresh centeredCrop() default over it.
    QImage wide(2880, 2037, QImage::Format_RGB32);
    stencil::core::CropRect dragged{100, 300, 800, 1200};   // off-centre, not the default
    CropPreview p(wide, 21.0, 29.7, dragged, nullptr, /*autoFitScreen=*/false);
    const auto before = p.cropRect();
    const double cx = before.x + before.width / 2.0, cy = before.y + before.height / 2.0;
    p.setAlbum(!p.album());
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
    const bool albumBefore = p.album();
    const auto before = p.cropRect();
    p.setAlbum(albumBefore);
    const auto after = p.cropRect();
    check(after.width == before.width && after.height == before.height,
          "setAlbum(same value) leaves an already-correct rect alone");
  }

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}
