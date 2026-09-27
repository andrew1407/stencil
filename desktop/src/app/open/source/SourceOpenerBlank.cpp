#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "SourceOpener.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "launchOptions.hpp"
#include "Notifications.hpp"
#include "../../../support/modal/imageAnchor.hpp"

#include <QImageReader>

// Local-file opens, blank-page creation and the crop dialog.

namespace stencil::gui {

  // Browser openImageHere: persist the current content first (unless incognito), then a fresh editor in the requested mode.
  void SourceOpener::openImageHere(const QString& path, bool incognito) {
    if (!w.incognito) {
      if (!w.activeProjectId.isEmpty()) w.saveToActiveProject();
      else w.saveSessionNow();
    }
    // Drop the project binding and adopt the incognito mode directly, signals blocked so the toggle slot doesn't fire.
    w.activeProjectId.clear();
    if (w.incognito != incognito) {
      w.incognito = incognito;
      w.overlays.incognito->setActive(incognito);
      w.acts.incognito->blockSignals(true);
      w.acts.incognito->setChecked(incognito);
      w.acts.incognito->blockSignals(false);
      w.projectTitle->updateProjectTitle();
    }
    loadLocalImageReset(path, [this](bool ok) {
      if (!ok) return;
      w.playImageArrival();
      w.adoptCanvasAsLocalProject();
    });
  }

  // A fresh window (the browser's "open in new tab"), via the launch path (--src/--incognito).
  void SourceOpener::openImageInNewWindow(const QString& path, bool incognito) {
    // The new window's async launch cannot report a failure back, so validate up front and report on THIS window.
    if (!QImageReader(path).canRead()) {
      w.notify->error("Failed to load image");
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

  void SourceOpener::createBlankImageFromDialog(const QColor& color, int width, int height) {
    if (w.canvas->hasImage()) {
      ConfirmSpec spec;
      spec.title = MainWindow::tr("Replace image");
      spec.message = MainWindow::tr("Replace the current image with a new blank image?");
      spec.confirmLabel = MainWindow::tr("Replace");
      spec.confirmIcon = QStringLiteral("refresh");
      spec.flight = openImageConfirmFlight(&w);
      if (!confirmModal(&w, spec)) return;
    }
    createBlankImage(color, width, height);
  }

  // No confirmation: a modal is something an op-plan cannot answer.
  void SourceOpener::createBlankImage(const QColor& color, int width, int height) {
    // A blank's colour IS the page: a leftover filter would repaint the fill, so start clean.
    if (w.settings.imageFilter != QLatin1String("none")) w.applyImageFilter("none", /*asUndoStep=*/false);
    const core::PageSize page = naturalPageCm(w.pageSizeValue(),
                                              w.settings.customPageWidth,
                                              w.settings.customPageHeight);
    w.canvas->setPageCm(page.width, page.height);
    // loadFromImage crops to the page aspect, so shape the fill that way here: the toast
    // then states the size the blank keeps, and no pixel is filled to be cropped off.
    const core::CropRect fit = core::centeredCrop(
        width, height, core::cropAspect(page.width, page.height, core::isAlbumOrientation(width, height)));
    const int fw = qMax(1, qRound(fit.width));
    const int fh = qMax(1, qRound(fit.height));
    QImage img(fw, fh, QImage::Format_RGB32);
    img.fill(color);
    w.activeProjectId.clear();  // a new blank is a fresh editor, not the old project
    w.canvas->loadFromImage(img);
    w.docSource.setBytes({}, {});  // synthetic blank → re-encode from pixels on bundle
    w.docSource.currentSource.clear();  // a generated blank image has no provenance
    w.docSource.currentResource.clear();
    w.docSource.blankColor = color.name();  // mark this session as a (recolourable) blank of this fill
    w.canvas->setBlankPage(true); // compare views keep a blank's fill + tint
    w.refreshActions();
    w.fitToWindow();   // BEFORE the dust measures it (browser settle.js), or a wide page's cloud fills the viewport
    w.playImageArrival();   // a blank is an image appearing, so it assembles like any other
    w.notify->success(QString("Blank %1×%2 image created").arg(fw).arg(fh));
    w.adoptCanvasAsLocalProject();  // persist so it appears in Projects (browser parity)
  }
}  // namespace stencil::gui
