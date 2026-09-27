#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "cropGeometry.hpp"
#include "CropDialog.hpp"
#include "modalReveal.hpp"
#include "Notifications.hpp"
#include "../../support/modal/modalChrome.hpp"

// Local-file opens, blank-page creation and the crop dialog.

namespace stencil::gui {



  // A solid-colour image adopted like a clipboard paste (browser blankImageModal.js); blank creation lives in the one Open dialog.
  void MainWindow::newBlankImage() { parts.sourceOpener.openImageDialog(/*startBlank=*/true); }



  // Browser cropModal.js: confirm before discarding lines when the orientation flips; the original is never replaced.
  void MainWindow::openCropDialog() {
    if (!canvas->hasImage()) {
      notify->error("Open an image first");
      return;
    }
    const core::PageSize page = naturalPageCm(
        pageSizeValue(), settings.customPageWidth, settings.customPageHeight);
    canvas->setPageCm(page.width, page.height);

    const core::CropRect cur = canvas->getCropRect();
    const bool album = core::isAlbumOrientation(cur.width, cur.height);
    // cropRect lives in the rotated original's pixel space.
    CropDialog dlg(canvas->effectiveOriginalImage(), page.width, page.height, album, cur, this);
    support::revealDialog(dlg, pop.dialogAnchor.data(), pop.dialogAnchorRect);   // flight + the dim behind
    if (dlg.exec() != QDialog::Accepted) return;

    const core::CropRect next = dlg.cropRect();
    const core::CropChange ch = core::cropChange(cur, next);
    if (ch.orientationChanged && !canvas->getLines().empty()) {
      ConfirmSpec spec;
      spec.title = tr("Change orientation");
      spec.message = tr("Changing the crop orientation will remove all placed lines and "
                        "points. Continue?");
      spec.danger = true;   // it destroys the placed lines
      if (!confirmModal(this, spec)) return;
    }
    const bool hadLines = !canvas->getLines().empty();
    canvas->applyCrop(next, /*recalc=*/true);
    fitToWindow();
    refreshActions();
    if (ch.orientationChanged && hadLines)
      notify->success("Image cropped — lines removed (orientation changed)");
    else
      notify->success("Image cropped");
  }

}  // namespace stencil::gui
