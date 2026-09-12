#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "cropGeometry.hpp"
#include "CropDialog.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "launchOptions.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ServerClient.hpp"
#include "../support/modalChrome.hpp"

#include <QImage>
#include <QImageReader>

// Local-file opens, blank-page creation and the crop dialog.

namespace stencil::gui {

  // Browser openImageHere: persist the current content first (unless incognito), then a fresh editor in the requested mode.
  void MainWindow::openImageHere(const QString& path, bool incognito) {
    if (!incognito_) {
      if (!activeProjectId_.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    // Drop the project binding and adopt the incognito mode directly, signals blocked so the toggle slot doesn't fire.
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

  // A fresh window (the browser's "open in new tab"), via the launch path (--src/--incognito).
  void MainWindow::openImageInNewWindow(const QString& path, bool incognito) {
    // The new window's async launch cannot report a failure back, so validate up front and report on THIS window.
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

  // A solid-colour image adopted like a clipboard paste (browser blankImageModal.js); blank creation lives in the one Open dialog.
  void MainWindow::newBlankImage() { openImageDialog(/*startBlank=*/true); }

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

  // No confirmation: a modal is something an op-plan cannot answer.
  void MainWindow::createBlankImage(const QColor& color, int w, int h) {
    // A blank's colour IS the page: a leftover filter would repaint the fill, so start clean.
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

  // Browser cropModal.js: confirm before discarding lines when the orientation flips; the original is never replaced.
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
    // cropRect lives in the rotated original's pixel space.
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
