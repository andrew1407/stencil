#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "SourceOpener.hpp"
#include "ServerClient.hpp"
#include "Notifications.hpp"
#include "mainWindowHelpers.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "launchOptions.hpp"
#include "../../../support/modal/imageAnchor.hpp"

#include <QFileInfo>
#include <memory>

// Opening an image: the open dialog and the here/new-window/preview entry points.

namespace stencil::gui {

  // Load a local file as a fresh image, clearing any source/resource provenance. The decode and the
  // byte re-read run on the pool; `done` hears whether the picture went in.
  void SourceOpener::loadLocalImageReset(const QString& path, std::function<void(bool)> done) {
    const QString ext = QFileInfo(path).suffix().toLower();
    auto bytes = std::make_shared<QByteArray>();
    w.decodeForCanvas(
        [path, ext, bytes] {
          if (!ext.isEmpty()) readFileBytes(path, *bytes);
          return QImage(path);
        },
        [this, path, ext, bytes, done](const QImage& decoded) {
          const core::PageSize page = naturalPageCm(w.pageSizeValue(),
                                                    w.settings.customPageWidth,
                                                    w.settings.customPageHeight);
          w.canvas->setPageCm(page.width, page.height);
          if (!w.canvas->loadImage(path, decoded)) {
            w.notify->error("Failed to load image");
            if (done) done(false);
            return;
          }
          w.docSource.setBytes(*bytes, ext);   // untouched file bytes ⇒ a lossless .stencil bundle
          w.docSource.currentSource.clear();  // a local file has no source/resource provenance
          w.docSource.currentResource.clear();
          w.docSource.blankColor.clear();     // a loaded image is not a blank project
          w.refreshActions();
          if (done) done(true);
        },
        [done] { if (done) done(false); });
  }

  // The unified Open dialog (browser openImageModal.js): file, URL, or a NEW BLANK canvas.
  void SourceOpener::openImageDialog(bool startBlank) {
    const auto px = core::defaultBlankSizePx(w.currentPageDimensions());
    OpenImageDialog dlg(&w, canReplaceActive(), px.width, px.height, startBlank,
                        w.settings.pageSize);
    if (w.remote.connections && !w.incognito) dlg.setServerTargets(w.remote.connections->urls());
    w.docSource.pendingServerTarget.clear();
    // Raised by neither toolbar half nor the idle card: the canvas stands in for the opener.
    if (!w.pop.dialogAnchor && !w.pop.dialogAnchorRect.isValid())
      w.pop.dialogAnchorRect = canvasAnchorRect(&w);
    QPointer<MainWindow> self(&w);
    w.pop.dialogCloseRect = [self](bool opened) {   // an accept always opened an image
      return opened ? openImageAnchorRect(self.data()) : QRect();
    };
    if (w.execMaybePopover(dlg) != QDialog::Accepted) return;
    if (dlg.getOutcome() == OpenImageDialog::Outcome::BLANK) {
      createBlankImageFromDialog(dlg.blankColor(), dlg.blankWidth(), dlg.blankHeight());
      return;
    }
    const QString src = dlg.source();
    if (src.isEmpty()) return;
    const OpenImageDialog::Outcome outcome = dlg.getOutcome();
    // Consumed by adoptCanvasAsLocalProject once the image lands; a new window has no session to hand it to.
    if (outcome == OpenImageDialog::Outcome::HERE) w.docSource.pendingServerTarget = dlg.serverTarget();

    // Preview path: adopt the pixels the dialog already decoded (no re-download/seek) and honour its Crop toggle.
    // Replace keeps its own in-place path; no preview falls back to the async resolve.
    const QImage previewed = dlg.previewedImage();
    if (!previewed.isNull() &&
        (outcome == OpenImageDialog::Outcome::HERE ||
         outcome == OpenImageDialog::Outcome::NEW_WINDOW)) {
      const bool localFile = !dlg.isUrl() && !dlg.isVideo() && QFileInfo(src).exists();
      if (outcome == OpenImageDialog::Outcome::NEW_WINDOW) {
        // The fresh window re-resolves the same source and applies the same page-aspect crop.
        openSourceInNewWindow(src, dlg.getFrame(), dlg.getIncognito(), /*fallbacks=*/{},
                              /*hasPreview=*/true, dlg.cropToPage(), dlg.getCropAlbum(),
                              dlg.getCropPageSize(), dlg.cropRect());
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
  void SourceOpener::openSourceHere(const QString& src, int frame, bool incognito,
                                  const QStringList& fallbacks) {
    if (!w.incognito) {
      if (!w.activeProjectId.isEmpty()) w.saveToActiveProject();
      else w.saveSessionNow();
    }
    w.activeProjectId.clear();
    if (w.incognito != incognito) {
      w.incognito = incognito;
      w.overlays.incognito->setActive(incognito);
      w.acts.incognito->blockSignals(true);
      w.acts.incognito->setChecked(incognito);
      w.acts.incognito->blockSignals(false);
      w.projectTitle->updateProjectTitle();
    }
    openImageSource(src, frame, fallbacks);  // async; failure is reported by MediaLoader
  }

  // "Open in new window" for a URL / local video: the source and quick-crop ride the launch options;
  // MediaLoader validates + reports in that window.
  void SourceOpener::openSourceInNewWindow(const QString& src, int frame, bool incognito,
                                         const QStringList& fallbacks, bool hasPreview,
                                         bool cropToPage, bool cropAlbum,
                                         const QString& cropPage,
                                         const core::CropRect& cropRect) {
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    LaunchOptions opts;
    opts.src = src;
    opts.srcFallbacks = fallbacks;
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
  void SourceOpener::openPreviewedImageHere(const QImage& image, const QString& localPath,
                                          const QString& provSource, bool incognito,
                                          bool cropToPage, bool cropAlbum,
                                          const QString& cropPage,
                                          const core::CropRect& cropRect) {
    if (!w.incognito) {
      if (!w.activeProjectId.isEmpty()) w.saveToActiveProject();
      else w.saveSessionNow();
    }
    w.activeProjectId.clear();
    if (w.incognito != incognito) {
      w.incognito = incognito;
      w.overlays.incognito->setActive(incognito);
      w.acts.incognito->blockSignals(true);
      w.acts.incognito->setChecked(incognito);
      w.acts.incognito->blockSignals(false);
      w.projectTitle->updateProjectTitle();
    }
    // Never the default page-aspect auto-crop: what was previewed is what opens.
    if (cropToPage)
      w.docSource.pendingCrop = {QuickCropOpts::Mode::PAGE, cropAlbum, cropPage, cropRect};
    else
      w.docSource.pendingCrop = QuickCropOpts::none();
    w.docSource.pendingProvSource = provSource;
    onLaunchImageLoaded(image, localPath);
  }

  void SourceOpener::openImage() { openImageDialog(/*startBlank=*/false); }
}  // namespace stencil::gui
