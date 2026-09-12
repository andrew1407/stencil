#include "mainWindow.hpp"
#include "serverClient.hpp"
#include "connectionStore.hpp"
#include "notifications.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "qtLlmTransport.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "overlayScrollArea.hpp"
#include "incognitoOverlay.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "../support/modalChrome.hpp"
#include "../support/rowWork.hpp"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>

// Opening an image: the open dialog and the here/new-window/preview entry points.

namespace stencil::gui {

  // Load a local file as a fresh image, clearing any source/resource provenance.
  bool MainWindow::loadLocalImageReset(const QString& path) {
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    // Decode and byte re-read are independent file jobs: the decode goes to the pool, this thread takes slice 0.
    QImage decoded;
    QByteArray sourceBytes;
    const QString ext = QFileInfo(path).suffix().toLower();
    support::forEachSlice(2, 1, [&](int i0, int i1) {
      for (int i = i0; i < i1; ++i) {
        if (i == 0) { if (!ext.isEmpty()) readFileBytes(path, sourceBytes); }
        else decoded.load(path);
      }
    });
    if (!canvas_->loadImage(path, decoded)) {
      notify_->error("Failed to load image");
      return false;
    }
    setSourceBytes(sourceBytes, ext);   // untouched file bytes ⇒ a lossless .stencil bundle
    currentSource_.clear();  // a local file has no source/resource provenance
    currentResource_.clear();
    blankColor_.clear();     // a loaded image is not a blank project
    refreshActions();
    return true;
  }

  void MainWindow::openImage() { openImageDialog(/*startBlank=*/false); }

  // The unified Open dialog (browser openImageModal.js): file, URL, or a NEW BLANK canvas.
  void MainWindow::openImageDialog(bool startBlank) {
    const auto px = core::defaultBlankSizePx(currentPageDimensions());
    OpenImageDialog dlg(this, canReplaceActive(), px.width, px.height, startBlank,
                        settings_.pageSize, settings_.units);
    if (connections_ && !incognito_) dlg.setServerTargets(connections_->urls());
    pendingServerTarget_.clear();
    if (execMaybePopover(dlg) != QDialog::Accepted) return;
    if (dlg.outcome() == OpenImageDialog::Outcome::BLANK) {
      createBlankImageFromDialog(dlg.blankColor(), dlg.blankWidth(), dlg.blankHeight());
      return;
    }
    const QString src = dlg.source();
    if (src.isEmpty()) return;
    const OpenImageDialog::Outcome outcome = dlg.outcome();
    // Consumed by adoptCanvasAsLocalProject once the image lands; a new window has no session to hand it to.
    if (outcome == OpenImageDialog::Outcome::HERE) pendingServerTarget_ = dlg.serverTarget();

    // Preview path: adopt the pixels the dialog already decoded (no re-download/seek) and honour its Crop toggle.
    // Replace keeps its own in-place path; no preview falls back to the async resolve.
    const QImage previewed = dlg.previewedImage();
    if (!previewed.isNull() &&
        (outcome == OpenImageDialog::Outcome::HERE ||
         outcome == OpenImageDialog::Outcome::NEW_WINDOW)) {
      const bool localFile = !dlg.isUrl() && !dlg.isVideo() && QFileInfo(src).exists();
      if (outcome == OpenImageDialog::Outcome::NEW_WINDOW) {
        // The fresh window re-resolves the same source and applies the same page-aspect crop.
        openSourceInNewWindow(src, dlg.frame(), dlg.incognito(), /*hasPreview=*/true,
                              dlg.cropToPage(), dlg.cropAlbum(), dlg.cropPageSize());
        return;
      }
      openPreviewedImageHere(previewed, localFile ? src : QString(),
                             dlg.isUrl() ? src : QString(), dlg.incognito(),
                             dlg.cropToPage(), dlg.cropAlbum(), dlg.cropPageSize());
      return;
    }

    if (dlg.isUrl() || dlg.isVideo()) {
      if (outcome == OpenImageDialog::Outcome::NEW_WINDOW)
        openSourceInNewWindow(src, dlg.frame(), dlg.incognito());
      else
        openSourceHere(src, dlg.frame(), dlg.incognito());
      return;
    }
    if (outcome == OpenImageDialog::Outcome::NEW_WINDOW) {
      openImageInNewWindow(src, dlg.incognito());
    } else if (outcome == OpenImageDialog::Outcome::REPLACE) {
      replaceProjectImage(src, dlg.rename(), dlg.keepAnnotations());
    } else {
      openImageHere(src, dlg.incognito());
    }
  }

  // "Open here" for a URL / local video: openImageHere's reset, but loaded via the async MediaLoader path.
  void MainWindow::openSourceHere(const QString& src, int frame, bool incognito) {
    if (!incognito_) {
      if (!activeProjectId_.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    activeProjectId_.clear();
    if (incognito_ != incognito) {
      incognito_ = incognito;
      incognitoOverlay_->setActive(incognito);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(incognito);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    openImageSource(src, frame);  // async; failure is reported by MediaLoader
  }

  // "Open in new window" for a URL / local video: the source and quick-crop ride the launch options;
  // MediaLoader validates + reports in that window.
  void MainWindow::openSourceInNewWindow(const QString& src, int frame, bool incognito,
                                         bool hasPreview, bool cropToPage,
                                         bool cropAlbum, const QString& cropPage) {
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    LaunchOptions opts;
    opts.src = src;
    opts.frame = frame;
    opts.incognito = incognito;
    // crop OFF ⇒ the whole frame (skip the default page-aspect auto-crop), matching "Open here".
    if (hasPreview) {
      opts.hasCropOverride = true;
      opts.cropToPage = cropToPage;
      opts.cropAlbum = cropAlbum;
      opts.cropPage = cropPage;
    }
    win->applyLaunchOptions(opts);
  }

  // Adopt the pixels the dialog already decoded, honouring its quick-crop; then the shared onLaunchImageLoaded adoption.
  void MainWindow::openPreviewedImageHere(const QImage& image, const QString& localPath,
                                          const QString& provSource, bool incognito,
                                          bool cropToPage, bool cropAlbum,
                                          const QString& cropPage) {
    if (!incognito_) {
      if (!activeProjectId_.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    activeProjectId_.clear();
    if (incognito_ != incognito) {
      incognito_ = incognito;
      incognitoOverlay_->setActive(incognito);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(incognito);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    // Never the default page-aspect auto-crop: what was previewed is what opens.
    if (cropToPage)
      pendingCrop_ = {QuickCropOpts::Mode::PAGE, cropAlbum, cropPage};
    else
      pendingCrop_ = {QuickCropOpts::Mode::NONE, false, QString()};
    pendingProvSource_ = provSource;
    onLaunchImageLoaded(image, localPath);
  }

}  // namespace stencil::gui
