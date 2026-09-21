#include "MainWindow.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "Notifications.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "QtLlmTransport.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "OverlayScrollArea.hpp"
#include "IncognitoOverlay.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "launchOptions.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/rowWork.hpp"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>

// Opening an image: the open dialog and the here/new-window/preview entry points.

namespace stencil::gui {

  // Load a local file as a fresh image, clearing any source/resource provenance.
  bool MainWindow::loadLocalImageReset(const QString& path) {
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings.customPageWidth,
                                              settings.customPageHeight);
    canvas->setPageCm(page.width, page.height);
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
    if (!canvas->loadImage(path, decoded)) {
      notify->error("Failed to load image");
      return false;
    }
    setSourceBytes(sourceBytes, ext);   // untouched file bytes ⇒ a lossless .stencil bundle
    currentSource.clear();  // a local file has no source/resource provenance
    currentResource.clear();
    blankColor.clear();     // a loaded image is not a blank project
    refreshActions();
    return true;
  }

  void MainWindow::openImage() { openImageDialog(/*startBlank=*/false); }

  // The unified Open dialog (browser openImageModal.js): file, URL, or a NEW BLANK canvas.
  void MainWindow::openImageDialog(bool startBlank) {
    const auto px = core::defaultBlankSizePx(currentPageDimensions());
    OpenImageDialog dlg(this, canReplaceActive(), px.width, px.height, startBlank,
                        settings.pageSize);
    if (connections && !incognito) dlg.setServerTargets(connections->urls());
    pendingServerTarget.clear();
    if (execMaybePopover(dlg) != QDialog::Accepted) return;
    if (dlg.getOutcome() == OpenImageDialog::Outcome::BLANK) {
      createBlankImageFromDialog(dlg.blankColor(), dlg.blankWidth(), dlg.blankHeight());
      return;
    }
    const QString src = dlg.source();
    if (src.isEmpty()) return;
    const OpenImageDialog::Outcome outcome = dlg.getOutcome();
    // Consumed by adoptCanvasAsLocalProject once the image lands; a new window has no session to hand it to.
    if (outcome == OpenImageDialog::Outcome::HERE) pendingServerTarget = dlg.serverTarget();

    // Preview path: adopt the pixels the dialog already decoded (no re-download/seek) and honour its Crop toggle.
    // Replace keeps its own in-place path; no preview falls back to the async resolve.
    const QImage previewed = dlg.previewedImage();
    if (!previewed.isNull() &&
        (outcome == OpenImageDialog::Outcome::HERE ||
         outcome == OpenImageDialog::Outcome::NEW_WINDOW)) {
      const bool localFile = !dlg.isUrl() && !dlg.isVideo() && QFileInfo(src).exists();
      if (outcome == OpenImageDialog::Outcome::NEW_WINDOW) {
        // The fresh window re-resolves the same source and applies the same page-aspect crop.
        openSourceInNewWindow(src, dlg.getFrame(), dlg.getIncognito(), /*hasPreview=*/true,
                              dlg.cropToPage(), dlg.getCropAlbum(), dlg.getCropPageSize(),
                              dlg.cropRect());
        return;
      }
      openPreviewedImageHere(previewed, localFile ? src : QString(),
                             dlg.isUrl() ? src : QString(), dlg.getIncognito(),
                             dlg.cropToPage(), dlg.getCropAlbum(), dlg.getCropPageSize(),
                             dlg.cropRect());
      return;
    }

    if (dlg.isUrl() || dlg.isVideo()) {
      if (outcome == OpenImageDialog::Outcome::NEW_WINDOW)
        openSourceInNewWindow(src, dlg.getFrame(), dlg.getIncognito());
      else
        openSourceHere(src, dlg.getFrame(), dlg.getIncognito());
      return;
    }
    if (outcome == OpenImageDialog::Outcome::NEW_WINDOW) {
      openImageInNewWindow(src, dlg.getIncognito());
    } else if (outcome == OpenImageDialog::Outcome::REPLACE) {
      replaceProjectImage(src, dlg.getRename(), dlg.keepAnnotations());
    } else {
      openImageHere(src, dlg.getIncognito());
    }
  }

  // "Open here" for a URL / local video: openImageHere's reset, but loaded via the async MediaLoader path.
  void MainWindow::openSourceHere(const QString& src, int frame, bool incognito) {
    if (!this->incognito) {
      if (!activeProjectId.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    activeProjectId.clear();
    if (this->incognito != incognito) {
      this->incognito = incognito;
      incognitoOverlay->setActive(incognito);
      actIncognito->blockSignals(true);
      actIncognito->setChecked(incognito);
      actIncognito->blockSignals(false);
      updateProjectTitle();
    }
    openImageSource(src, frame);  // async; failure is reported by MediaLoader
  }

  // "Open in new window" for a URL / local video: the source and quick-crop ride the launch options;
  // MediaLoader validates + reports in that window.
  void MainWindow::openSourceInNewWindow(const QString& src, int frame, bool incognito,
                                         bool hasPreview, bool cropToPage,
                                         bool cropAlbum, const QString& cropPage,
                                         const core::CropRect& cropRect) {
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
      // What the user DRAGGED rides along, so the new window shows the same box rather
      // than re-centring one (browser twin: openOpts()'s crop reaches openImageNewTab).
      opts.cropX = cropRect.x;
      opts.cropY = cropRect.y;
      opts.cropW = cropRect.width;
      opts.cropH = cropRect.height;
    }
    win->applyLaunchOptions(opts);
  }

  // Adopt the pixels the dialog already decoded, honouring its quick-crop; then the shared onLaunchImageLoaded adoption.
  void MainWindow::openPreviewedImageHere(const QImage& image, const QString& localPath,
                                          const QString& provSource, bool incognito,
                                          bool cropToPage, bool cropAlbum,
                                          const QString& cropPage,
                                          const core::CropRect& cropRect) {
    if (!this->incognito) {
      if (!activeProjectId.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    activeProjectId.clear();
    if (this->incognito != incognito) {
      this->incognito = incognito;
      incognitoOverlay->setActive(incognito);
      actIncognito->blockSignals(true);
      actIncognito->setChecked(incognito);
      actIncognito->blockSignals(false);
      updateProjectTitle();
    }
    // Never the default page-aspect auto-crop: what was previewed is what opens.
    if (cropToPage)
      pendingCrop = {QuickCropOpts::Mode::PAGE, cropAlbum, cropPage, cropRect};
    else
      pendingCrop = {QuickCropOpts::Mode::NONE, false, QString()};
    pendingProvSource = provSource;
    onLaunchImageLoaded(image, localPath);
  }

}  // namespace stencil::gui
