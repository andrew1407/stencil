#include "mainWindow.hpp"
#include "fetchGuard.hpp"
#include "notifications.hpp"
#include <QComboBox>
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "cropGeometry.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "dataExportController.hpp"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QUrl>

// What happens once a launch image has decoded: quick crop and the layout overlay.

namespace stencil::gui {

  // Adopt a resolved --src image. A local file keeps its path (so session/project
  // saves reference it); a remote image / video frame has no path, so it is
  // adopted in-memory (like a clipboard paste). The page aspect is applied first,
  // exactly as openImage() does, so the auto-crop matches the current page size.
  void MainWindow::onLaunchImageLoaded(const QImage& image,
                                       const QString& localPath) {
    // Provenance for this load (from loadImageByUrl); consumed once, then cleared.
    const QString provSource = pendingProvSource_;
    const QString provResource = pendingProvResource_;
    pendingProvSource_.clear();
    pendingProvResource_.clear();

    // Inline full-layout hand-off (browser→desktop "Open in…" of a local/incognito
    // project): the layout describes crop + rotation + filter + lines + page in the
    // ORIGINAL image's space, so adopt it exactly like a server project — NOT the
    // default auto-crop + lines-only import, which would prompt on a dimension mismatch,
    // drop the lines, and ignore the filter. The image always arrives in-memory (a
    // data: URL decoded in openImageSource), so `image` is set here.
    if (!pendingLaunchLayoutJson_.isEmpty()) {
      const QString json = pendingLaunchLayoutJson_;
      pendingLaunchLayoutJson_.clear();
      pendingLaunchLayout_.clear();   // an inline layout supersedes any --layout source
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
      if (err.error == QJsonParseError::NoError && doc.isObject() && !image.isNull()) {
        loadImageWithLayout(image, doc.object());
        currentSource_ = provSource;
        currentResource_ = provResource;
        refreshActions();
        fitToWindow();
        return;
      }
      notify_->error("Invalid layout in the stencil:// link"
                     + (err.error != QJsonParseError::NoError ? QStringLiteral(": ") + err.errorString()
                                                              : QString()));
      // Fall through to a plain image load below.
    }

    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    bool ok = false;
    if (!localPath.isEmpty())
      ok = canvas_->loadImage(localPath, image);  // path-backed (keeps it for saves)
    if (ok) {
      retainSourceFromFile(localPath);   // lossless .stencil bundle from the untouched file
    } else {
      if (image.isNull()) {
        notify_->error("Failed to open the image");
        pendingLaunchLayout_.clear();
        return;
      }
      canvas_->loadFromImage(image);  // remote image / video frame (in-memory)
      setSourceBytes({}, {});         // in-memory frame/remote → re-encode on bundle
    }
    // Quick pre-load crop (links modal): override the default page-aspect auto-crop
    // with the chosen page + orientation, or load the full frame uncropped. Applies
    // equally to still images and extracted video frames. Consumed once.
    applyQuickCrop();
    // A new image's provenance replaces the previous one (a plain --src/OS open
    // carries none, so both clear). Saved to the project on the next create/save.
    currentSource_ = provSource;
    currentResource_ = provResource;
    activeProjectId_.clear();  // a fresh URL/video/OS-open load is a new editor
    refreshActions();
    fitToWindow();
    playImageArrival();

    // --layout: apply now that an image exists (applyLayoutJson needs one). This is the
    // path/URL --layout variant; the inline stencil:// layout is handled up top via the
    // full-adoption branch.
    if (!pendingLaunchLayout_.isEmpty()) {
      const QString src = pendingLaunchLayout_;
      pendingLaunchLayout_.clear();
      applyLayoutFromSource(src);
    }
    // Persist as a local project so it shows in Projects (after any --layout lines are
    // in). A remote image / video frame has no on-disk path — createLocalProject writes
    // the pixels to the state dir. Browser parity: the active editor is always saved.
    adoptCanvasAsLocalProject();
  }

  // Override the just-loaded image's default page-aspect crop with the quick-crop
  // choice from the load-by-URL dialog (extends the existing auto-crop with an
  // orientation override + page choice + a no-crop path). Mirrors the browser's
  // defaultCropRect(albumOverride) / noCrop load opts. No-op for Auto / no image.
  void MainWindow::applyQuickCrop() {
    const QuickCropOpts opts = pendingCrop_;
    pendingCrop_ = {};  // consume regardless of outcome
    if (!canvas_->hasImage() || opts.mode == QuickCropOpts::Mode::Auto) return;
    // A freshly loaded image is un-rotated, so the original IS the crop's pixel space.
    const QImage& orig = canvas_->originalImage();
    const double iw = orig.width();
    const double ih = orig.height();
    if (iw <= 0 || ih <= 0) return;
    if (opts.mode == QuickCropOpts::Mode::None) {
      canvas_->applyCrop({0.0, 0.0, iw, ih}, /*recalc=*/false);  // full frame, uncropped
      return;
    }
    // Page mode: reflect the chosen page in the toolbar control (keeps coords/crop
    // dialog consistent), then crop to that page in the chosen orientation.
    if (!opts.page.isEmpty()) {
      const int idx = units_.pageSize->findData(opts.page);
      if (idx >= 0) units_.pageSize->setCurrentIndex(idx);  // → onPageSizeChanged
    }
    const core::PageSize pg = naturalPageCm(pageSizeValue(),
                                            settings_.customPageWidth,
                                            settings_.customPageHeight);
    canvas_->setPageCm(pg.width, pg.height);
    const double aspect = core::cropAspect(pg.width, pg.height, opts.album);
    canvas_->applyCrop(core::centeredCrop(iw, ih, aspect), /*recalc=*/false);
  }

  // Load a layout JSON from a local path or an http(s) URL, then adopt it through
  // the shared applyLayoutJson() guards. Mirrors uploadLayout(), but the source is
  // given (no file dialog) and may be remote.
  void MainWindow::applyLayoutFromSource(const QString& src) {
    auto adopt = [this, src](const QByteArray& bytes) {
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
      if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        notify_->error("Invalid layout JSON: " + err.errorString());
        return;
      }
      dataExport_->applyLayoutJson(doc.object());
    };

    const QUrl url = QUrl::fromUserInput(src);
    if (url.scheme() == "http" || url.scheme() == "https") {
      // --layout is a URL the USER named, so the loose guard: their own localhost server
      // stays reachable, the internal ranges do not.
      stencil::net::fetchGuard::get(this, url, /*strict=*/false,
                                    [this, adopt](const QByteArray& b, const QString& e) {
                                      if (e.isEmpty()) adopt(b);
                                      else notify_->error("Could not fetch --layout: " + e);
                                    });
      return;
    }
    // Local file (resolve the existing path, not fromUserInput's guess).
    const QString path =
        QFileInfo(src).exists() ? src : url.toLocalFile();
    QFile f(path.isEmpty() ? src : path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify_->error("Could not read --layout file");
      return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    adopt(bytes);
  }

}  // namespace stencil::gui
