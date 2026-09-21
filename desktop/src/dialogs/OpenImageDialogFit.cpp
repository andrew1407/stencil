#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include <algorithm>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>

// The preview's box: how big the picture (or the crop stage) may be, and how it gives up
// height when the whole dialog would otherwise run past the screen.
namespace stencil::gui {

  // Floored at PREVIEW_MAX_W/H, grown with a wider window, never past the screen, capped by
  // size.previewCapH. From width(), the WINDOW's: bodyContent's is laid out FROM this box.
  QSize OpenImageDialog::previewFitBox() const {
    int w = std::max(PREVIEW_MAX_W, width() - 2 * PREVIEW_COL_GAP - 40);
    int h = std::max(PREVIEW_MAX_H, w * PREVIEW_MAX_H / PREVIEW_MAX_W);
    if (QScreen* scr = screen()) h = std::min(h, int(scr->availableGeometry().height() * 0.6));
    if (size.previewCapH > 0) h = std::min(h, size.previewCapH);
    return { w, h };
  }

  // Re-fit the picture and the stage into the current box, from the ORIGINAL pixels
  // (never the already-shrunk label pixmap); the scrub bar keeps the picture's width.
  void OpenImageDialog::applyPreviewFit() {
    const QSize box = previewFitBox();
    previewLabel->setMaximumSize(box);
    if (!previewImage.isNull() && previewLabel->isVisible())
      previewLabel->setPixmap(QPixmap::fromImage(previewImage).scaled(
          box.width(), box.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (cropStage) {
      cropStage->setFitBox(box);
      if (previewIsVideo) frameSlider->setFixedWidth(cropStage->paintedRect().width());
    }
  }

  // The dialog wants `over` px more than the screen allows: the PICTURE gives them up, not the body
  // (user report; browser max-height 38vh). Under PREVIEW_MIN_H the body scrolls instead.
  int OpenImageDialog::shrinkPreviewToFit(int over) {
    const int picH = cropStage ? cropStage->paintedRect().height()
                                : (previewLabel->isVisible() ? previewLabel->pixmap().height() : 0);
    const int target = picH - over;
    if (target >= PREVIEW_MIN_H) {
      size.previewCapH = target;
      applyPreviewFit();
      if (size.bodyContent) {
        size.bodyContent->updateGeometry();
        if (QLayout* cl = size.bodyContent->layout()) { cl->invalidate(); cl->activate(); }
      }
      if (QLayout* l = layout()) { l->invalidate(); l->activate(); }
    }
    return std::max(wantedHeight(), size.floorH);
  }

  // A wider window fits a bigger preview. size.floorH <= 0: showEvent's own first-show resize
  // lands here before it sets the floor, and a refit then settled the window short.
  void OpenImageDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (!size.bodyContent || !previewLabel || measuring || !measured || size.floorH <= 0) return;
    const int w = width();
    if (w == size.previewFitWidth) return;
    size.previewFitWidth = w;
    size.previewCapH = 0;   // a new width, a fresh fit: the refit below re-derives any cap
    applyPreviewFit();
    // Deferred and coalesced: refitWindowHeight() resizes the window itself, and the height
    // ease's per-frame resize re-entered this handler and restarted the flight every frame.
    if (!size.refitPending) {
      size.refitPending = true;
      QPointer<OpenImageDialog> guard(this);
      QTimer::singleShot(0, this, [guard] {
        if (!guard) return;
        guard->size.refitPending = false;
        guard->refitWindowHeight();
      });
    }
  }

}  // namespace stencil::gui
