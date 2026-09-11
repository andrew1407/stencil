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

  // Load a local file as a fresh image, page-sized to the current page setting, clearing
  // any source/resource provenance. Notifies + refreshes actions. Returns whether it loaded.
  bool MainWindow::loadLocalImageReset(const QString& path) {
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    // Decoding the picture and re-reading its bytes for the bundle are independent file
    // jobs, so they run as two slices: the decode goes to the pool, this thread takes
    // slice 0. Neither touches GUI state; the canvas is handed the finished pixels.
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

  // File ▸ Open (the single top-left entry) opens the unified dialog in file/URL mode.
  void MainWindow::openImage() { openImageDialog(/*startBlank=*/false); }

  // The unified Open dialog (mirrors browser openImageModal.js): a local file, a web
  // URL/reference, or a NEW BLANK canvas. `startBlank` opens straight in blank mode
  // (the idle-canvas + projects "new blank" shortcuts). Dispatches on the chosen outcome.
  void MainWindow::openImageDialog(bool startBlank) {
    const auto px = core::defaultBlankSizePx(currentPageDimensions());
    OpenImageDialog dlg(this, canReplaceActive(), px.width, px.height, startBlank,
                        settings_.pageSize, settings_.units);
    // The browser's "Save to" row: a connected server as the new project's home.
    if (connections_ && !incognito_) dlg.setServerTargets(connections_->urls());
    pendingServerTarget_.clear();
    if (execMaybePopover(dlg) != QDialog::Accepted) return;
    if (dlg.outcome() == OpenImageDialog::Outcome::Blank) {
      createBlankImageFromDialog(dlg.blankColor(), dlg.blankWidth(), dlg.blankHeight());
      return;
    }
    const QString src = dlg.source();
    if (src.isEmpty()) return;
    const OpenImageDialog::Outcome outcome = dlg.outcome();
    // Consumed by adoptCanvasAsLocalProject once the image lands (sync or async) —
    // the fresh project is created on that server instead of locally. A new window
    // has no server session to hand it to; it saves locally as before.
    if (outcome == OpenImageDialog::Outcome::Here) pendingServerTarget_ = dlg.serverTarget();

    // Preview path: the dialog already decoded the exact image/frame and chose a
    // quick-crop. Adopt those pixels directly — no re-download/seek — and honor the
    // Crop toggle (on ⇒ crop centered to the chosen page/orientation; off ⇒ open the
    // whole image). Consumed exactly like openLinks() consumes LinksDialog. Applies to
    // the "open as new" outcomes (Here / new window); Replace keeps its own in-place
    // path below. Falls back to the async resolve when no preview was made.
    const QImage previewed = dlg.previewedImage();
    if (!previewed.isNull() &&
        (outcome == OpenImageDialog::Outcome::Here ||
         outcome == OpenImageDialog::Outcome::NewWindow)) {
      const bool localFile = !dlg.isUrl() && !dlg.isVideo() && QFileInfo(src).exists();
      if (outcome == OpenImageDialog::Outcome::NewWindow) {
        // The fresh window re-resolves the same source (identical pixels) and applies
        // the same page-aspect crop, so preview + crop carry across without moving pixels.
        openSourceInNewWindow(src, dlg.frame(), dlg.incognito(), /*hasPreview=*/true,
                              dlg.cropToPage(), dlg.cropAlbum(), dlg.cropPageSize());
        return;
      }
      openPreviewedImageHere(previewed, localFile ? src : QString(),
                             dlg.isUrl() ? src : QString(), dlg.incognito(),
                             dlg.cropToPage(), dlg.cropAlbum(), dlg.cropPageSize());
      return;
    }

    // No preview was taken (source typed but never previewed): keep the original async
    // resolve for a URL/video and the synchronous local-image load otherwise.
    if (dlg.isUrl() || dlg.isVideo()) {
      if (outcome == OpenImageDialog::Outcome::NewWindow)
        openSourceInNewWindow(src, dlg.frame(), dlg.incognito());
      else
        openSourceHere(src, dlg.frame(), dlg.incognito());
      return;
    }
    // A plain local image loads synchronously.
    if (outcome == OpenImageDialog::Outcome::NewWindow) {
      openImageInNewWindow(src, dlg.incognito());
    } else if (outcome == OpenImageDialog::Outcome::Replace) {
      replaceProjectImage(src, dlg.rename(), dlg.keepAnnotations());
    } else {
      openImageHere(src, dlg.incognito());
    }
  }

  // "Open here" for a URL / local video: mirror openImageHere's reset (persist the
  // current editor, drop the project binding, adopt the incognito choice) but load
  // via the async MediaLoader path. onLaunchImageLoaded then adopts a new project
  // (a no-op while incognito).
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

  // "Open in new window" for a URL / local video: spawn a fresh window and hand it
  // the source via launch options (same vehicle as openImageInNewWindow, minus the
  // local-only QImageReader guard — MediaLoader validates + reports in that window).
  // A quick-crop override (from the Open-Image dialog's preview) rides along so the new
  // window applies the identical page-aspect crop after re-resolving the same source.
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
    // With a preview taken, carry the exact crop choice; crop OFF ⇒ open the whole
    // frame (skip the default page-aspect auto-crop), matching the "Open here" path.
    if (hasPreview) {
      opts.hasCropOverride = true;
      opts.cropToPage = cropToPage;
      opts.cropAlbum = cropAlbum;
      opts.cropPage = cropPage;
    }
    win->applyLaunchOptions(opts);
  }

  // Adopt the pixels the Open-Image dialog already decoded for its preview (no second
  // download/seek), honoring its quick-crop. Mirrors openSourceHere's editor reset
  // (persist the current editor, drop the project binding, adopt the incognito choice),
  // then routes the in-memory image through the shared onLaunchImageLoaded adoption —
  // exactly as openLinks() does with LinksDialog's previewed image.
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
    // Crop choice: page-aspect crop, or the whole frame (crop off) — never the default
    // page-aspect auto-crop, so what was previewed is what opens.
    if (cropToPage)
      pendingCrop_ = {QuickCropOpts::Mode::Page, cropAlbum, cropPage};
    else
      pendingCrop_ = {QuickCropOpts::Mode::None, false, QString()};
    pendingProvSource_ = provSource;
    onLaunchImageLoaded(image, localPath);
  }

}  // namespace stencil::gui
