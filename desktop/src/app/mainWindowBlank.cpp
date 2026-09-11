#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "incognitoOverlay.hpp"
#include "cropGeometry.hpp"
#include "cropDialog.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "launchOptions.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "serverClient.hpp"
#include "../support/modalChrome.hpp"

#include <QImage>
#include <QImageReader>

// Local-file opens, blank-page creation and the crop dialog.

namespace stencil::gui {

  // Replace this editor's image with `path`. Mirrors the browser's openImageHere:
  // persist the current content first (unless incognito) so it isn't lost, then
  // start a fresh editor in the requested incognito mode and load the image.
  void MainWindow::openImageHere(const QString& path, bool incognito) {
    if (!incognito_) {
      if (!activeProjectId_.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    // Replacing the image wholesale resets the editor: drop the project binding and adopt
    // the chosen incognito mode directly (the toggle is normally gated to before an image).
    // Its signals are blocked so the toggle slot doesn't fire; we sync the title ourselves.
    activeProjectId_.clear();
    if (incognito_ != incognito) {
      incognito_ = incognito;
      incognitoOverlay_->setActive(incognito);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(incognito);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    if (loadLocalImageReset(path)) { playImageArrival(); adoptCanvasAsLocalProject(); }
  }

  // Launch `path` in a fresh, self-owned window, leaving this editor untouched
  // (the desktop analog of the browser's "open in new tab"). Reuses the launch
  // path (--src/--incognito), which honors incognito and the page-aspect crop.
  void MainWindow::openImageInNewWindow(const QString& path, bool incognito) {
    // The dialog only yields a local image file, and the new window's async launch path
    // can't report a load failure back here — so validate up front and show the error on
    // THIS window instead of spawning a blank one (mirrors openProjectInNewWindow's guard).
    if (!QImageReader(path).canRead()) {
      notify_->error("Failed to load image");
      return;
    }
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    LaunchOptions opts;
    opts.src = path;
    opts.incognito = incognito;
    win->applyLaunchOptions(opts);
  }

  // Generate a solid-color image and adopt it exactly like a clipboard paste
  // (confirm-replace guard included), so editing/persistence behave as if the
  // image had been opened from disk. Mirrors browser blankImageModal.js.
  // The idle-canvas + projects "new blank" shortcuts open the unified Open dialog
  // straight in blank mode (blank creation is folded into the one Open dialog).
  void MainWindow::newBlankImage() { openImageDialog(/*startBlank=*/true); }

  // Generate a solid-color blank image from the unified dialog's blank mode and adopt
  // it (was the body of the retired standalone blank-image dialog flow).
  void MainWindow::createBlankImageFromDialog(const QColor& color, int w, int h) {
    if (canvas_->hasImage()) {
      ConfirmSpec spec;
      spec.title = tr("Replace image");
      spec.message = tr("Replace the current image with a new blank image?");
      spec.confirmLabel = tr("Replace");
      spec.confirmIcon = QStringLiteral("refresh");
      if (!confirmModal(this, spec)) return;
    }
    createBlankImage(color, w, h);
  }

  // The blank itself, with no confirmation: an op-plan already said what to do, and a
  // modal is something a plan cannot answer — it would stall the turn half-applied.
  void MainWindow::createBlankImage(const QColor& color, int w, int h) {
    // A blank's colour IS the page: a filter left over from the previous image
    // would repaint the fill (bw of a red page is flat gray), so start clean.
    if (settings_.imageFilter != QLatin1String("none")) applyImageFilter("none");
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    {
      const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    activeProjectId_.clear();  // a new blank is a fresh editor, not the old project
    canvas_->loadFromImage(img);
    setSourceBytes({}, {});  // synthetic blank → re-encode from pixels on bundle
    currentSource_.clear();  // a generated blank image has no provenance
    currentResource_.clear();
    blankColor_ = color.name();  // mark this session as a (recolourable) blank of this fill
    canvas_->setBlankPage(true); // compare views keep a blank's fill + tint
    refreshActions();
    playImageArrival();   // a blank is an image appearing, so it assembles like any other
    notify_->success(QString("Blank %1×%2 image created").arg(w).arg(h));
    adoptCanvasAsLocalProject();  // persist so it appears in Projects (browser parity)
  }

  // Open the crop dialog over the ORIGINAL image and apply the chosen page-shaped
  // region. Mirrors browser cropModal.js: confirm before discarding lines when the
  // orientation flips; the original image is never replaced. Resizing within the
  // same orientation rescales the lines (the page relation is preserved).
  void MainWindow::openCropDialog() {
    if (!canvas_->hasImage()) {
      notify_->error("Open an image first");
      return;
    }
    const core::PageSize page = naturalPageCm(
        pageSizeValue(), settings_.customPageWidth, settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);

    const core::CropRect cur = canvas_->cropRect();
    const bool album = core::isAlbumOrientation(cur.width, cur.height);
    // Preview the rotated original — cropRect lives in that pixel space.
    CropDialog dlg(canvas_->effectiveOriginalImage(), page.width, page.height, album, cur, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const core::CropRect next = dlg.cropRect();
    const core::CropChange ch = core::cropChange(cur, next);
    if (ch.orientationChanged && !canvas_->lines().empty()) {
      ConfirmSpec spec;
      spec.title = tr("Change orientation");
      spec.message = tr("Changing the crop orientation will remove all placed lines and "
                        "points. Continue?");
      spec.danger = true;   // it destroys the placed lines
      if (!confirmModal(this, spec)) return;
    }
    const bool hadLines = !canvas_->lines().empty();
    canvas_->applyCrop(next, /*recalc=*/true);
    fitToWindow();
    refreshActions();
    if (ch.orientationChanged && hadLines)
      notify_->success("Image cropped — lines removed (orientation changed)");
    else
      notify_->success("Image cropped");
  }

}  // namespace stencil::gui
