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
#include "../../support/modal/modalChrome.hpp"

#include <QImage>
#include <QImageReader>

// Local-file opens, blank-page creation and the crop dialog.

namespace stencil::gui {

  // Browser openImageHere: persist the current content first (unless incognito), then a fresh editor in the requested mode.
  void MainWindow::openImageHere(const QString& path, bool incognito) {
    if (!this->incognito) {
      if (!activeProjectId.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    // Drop the project binding and adopt the incognito mode directly, signals blocked so the toggle slot doesn't fire.
    activeProjectId.clear();
    if (this->incognito != incognito) {
      this->incognito = incognito;
      incognitoOverlay->setActive(incognito);
      actIncognito->blockSignals(true);
      actIncognito->setChecked(incognito);
      actIncognito->blockSignals(false);
      updateProjectTitle();
    }
    if (loadLocalImageReset(path)) { playImageArrival(); adoptCanvasAsLocalProject(); }
  }

  // A fresh window (the browser's "open in new tab"), via the launch path (--src/--incognito).
  void MainWindow::openImageInNewWindow(const QString& path, bool incognito) {
    // The new window's async launch cannot report a failure back, so validate up front and report on THIS window.
    if (!QImageReader(path).canRead()) {
      notify->error("Failed to load image");
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
    if (canvas->hasImage()) {
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
    if (settings.imageFilter != QLatin1String("none")) applyImageFilter("none");
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings.customPageWidth,
                                              settings.customPageHeight);
    canvas->setPageCm(page.width, page.height);
    // loadFromImage crops to the page aspect, so shape the fill that way here: the toast
    // then states the size the blank keeps, and no pixel is filled to be cropped off.
    const core::CropRect fit = core::centeredCrop(
        w, h, core::cropAspect(page.width, page.height, core::isAlbumOrientation(w, h)));
    const int fw = qMax(1, qRound(fit.width));
    const int fh = qMax(1, qRound(fit.height));
    QImage img(fw, fh, QImage::Format_RGB32);
    img.fill(color);
    activeProjectId.clear();  // a new blank is a fresh editor, not the old project
    canvas->loadFromImage(img);
    setSourceBytes({}, {});  // synthetic blank → re-encode from pixels on bundle
    currentSource.clear();  // a generated blank image has no provenance
    currentResource.clear();
    blankColor = color.name();  // mark this session as a (recolourable) blank of this fill
    canvas->setBlankPage(true); // compare views keep a blank's fill + tint
    refreshActions();
    playImageArrival();   // a blank is an image appearing, so it assembles like any other
    notify->success(QString("Blank %1×%2 image created").arg(fw).arg(fh));
    adoptCanvasAsLocalProject();  // persist so it appears in Projects (browser parity)
  }

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
