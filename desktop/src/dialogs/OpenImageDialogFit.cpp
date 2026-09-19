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

  // Floored at PREVIEW_MAX_W/H, grown with a wider window, never past the screen — and
  // capped by size_.previewCapH once the picture has had to give up room (shrinkPreviewToFit).
  // From width(), the WINDOW's own: size_.bodyContent's width is laid out FROM the stage this
  // box fits, so measuring it fed the last box back in as the next one (a runaway climb).
  QSize OpenImageDialog::previewFitBox() const {
    int w = std::max(PREVIEW_MAX_W, width() - 2 * PREVIEW_COL_GAP - 40);
    int h = std::max(PREVIEW_MAX_H, w * PREVIEW_MAX_H / PREVIEW_MAX_W);
    if (QScreen* scr = screen()) h = std::min(h, int(scr->availableGeometry().height() * 0.6));
    if (size_.previewCapH > 0) h = std::min(h, size_.previewCapH);
    return { w, h };
  }

  // Re-fit the picture and the stage into the current box, from the ORIGINAL pixels
  // (never the already-shrunk label pixmap); the scrub bar keeps the picture's width.
  void OpenImageDialog::applyPreviewFit() {
    const QSize box = previewFitBox();
    previewLabel_->setMaximumSize(box);
    if (!previewImage_.isNull() && previewLabel_->isVisible())
      previewLabel_->setPixmap(QPixmap::fromImage(previewImage_).scaled(
          box.width(), box.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (cropStage_) {
      cropStage_->setFitBox(box);
      if (previewIsVideo_) frameSlider_->setFixedWidth(cropStage_->paintedRect().width());
    }
  }

  // The dialog wants `over` px more than the screen allows: the PICTURE gives them up, not
  // the body its bottom edge — a few px of overflow raised a scrollbar over nothing (user
  // report; browser twin: the preview's max-height 38vh). Returns the re-measured height.
  // Under PREVIEW_MIN_H the picture is left alone and the body scrolls after all.
  int OpenImageDialog::shrinkPreviewToFit(int over) {
    const int picH = cropStage_ ? cropStage_->paintedRect().height()
                                : (previewLabel_->isVisible() ? previewLabel_->pixmap().height() : 0);
    const int target = picH - over;
    if (target >= PREVIEW_MIN_H) {
      size_.previewCapH = target;
      applyPreviewFit();
      if (size_.bodyContent) {
        size_.bodyContent->updateGeometry();
        if (QLayout* cl = size_.bodyContent->layout()) { cl->invalidate(); cl->activate(); }
      }
      if (QLayout* l = layout()) { l->invalidate(); l->activate(); }
    }
    return std::max(wantedHeight(), size_.floorH);
  }

  // A wider window fits a bigger preview. size_.floorH <= 0: showEvent's own first-show resize
  // lands here before it sets the floor, and a refit then settled the window short.
  void OpenImageDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (!size_.bodyContent || !previewLabel_ || measuring_ || !measured_ || size_.floorH <= 0) return;
    const int w = width();
    if (w == size_.previewFitWidth) return;
    size_.previewFitWidth = w;
    size_.previewCapH = 0;   // a new width, a fresh fit: the refit below re-derives any cap
    applyPreviewFit();
    // Deferred and coalesced: refitWindowHeight() resizes the window itself, and the height
    // ease's per-frame resize re-entered this handler and restarted the flight every frame.
    if (!size_.refitPending) {
      size_.refitPending = true;
      QPointer<OpenImageDialog> guard(this);
      QTimer::singleShot(0, this, [guard] {
        if (!guard) return;
        guard->size_.refitPending = false;
        guard->refitWindowHeight();
      });
    }
  }

}  // namespace stencil::gui
