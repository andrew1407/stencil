#include "MainWindow.hpp"
#include "SourceOpener.hpp"
#include "fetchGuard.hpp"
#include "Notifications.hpp"
#include <QComboBox>
#include "naturalPage.hpp"
#include "CanvasWidget.hpp"
#include "DataExportController.hpp"

#include <QFileInfo>

// What happens once a launch image has decoded: quick crop and the layout overlay.

namespace stencil::gui {

  // Adopt a resolved --src image: a local file keeps its path, a remote image / video frame is adopted in-memory. Page aspect first, as openImage().
  void SourceOpener::onLaunchImageLoaded(const QImage& image,
                                       const QString& localPath) {
    // Provenance from loadImageByUrl; consumed once.
    const QString provSource = w.projectHost.docSource.pendingProvSource;
    const QString provResource = w.projectHost.docSource.pendingProvResource;
    w.projectHost.docSource.pendingProvSource.clear();
    w.projectHost.docSource.pendingProvResource.clear();

    // Inline full-layout hand-off (browser→desktop "Open in…"): crop + rotation + filter + lines + page in the ORIGINAL
    // image's space, adopted like a server project — NOT the auto-crop + lines-only import.
    if (!w.projectHost.docSource.pendingLaunchLayoutJson.isEmpty()) {
      const QString json = w.projectHost.docSource.pendingLaunchLayoutJson;
      w.projectHost.docSource.pendingLaunchLayoutJson.clear();
      w.projectHost.docSource.pendingLaunchLayout.clear();   // an inline layout supersedes any --layout source
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
      if (err.error == QJsonParseError::NoError && doc.isObject() && !image.isNull()) {
        w.loadImageWithLayout(image, doc.object());
        w.projectHost.docSource.currentSource = provSource;
        w.projectHost.docSource.currentResource = provResource;
        w.refreshActions();
        w.fitToWindow();
        return;
      }
      w.notify->error("Invalid layout in the stencil:// link"
                     + (err.error != QJsonParseError::NoError ? QStringLiteral(": ") + err.errorString()
                                                              : QString()));
    }

    const core::PageSize page = naturalPageCm(w.pageSizeValue(),
                                              w.settings.customPageWidth,
                                              w.settings.customPageHeight);
    w.canvas->setPageCm(page.width, page.height);
    bool ok = false;
    if (!localPath.isEmpty())
      ok = w.canvas->loadImage(localPath, image);  // path-backed (keeps it for saves)
    if (ok) {
      retainSourceFromFile(localPath);   // lossless .stencil bundle from the untouched file
    } else {
      if (image.isNull()) {
        w.notify->error("Failed to open the image");
        w.projectHost.docSource.pendingLaunchLayout.clear();
        return;
      }
      w.canvas->loadFromImage(image);  // remote image / video frame (in-memory)
      w.projectHost.docSource.setBytes({}, {});         // in-memory frame/remote → re-encode on bundle
    }
    // Quick pre-load crop from the links modal overrides the page-aspect auto-crop; consumed once.
    applyQuickCrop();
    // A new image's provenance replaces the previous one.
    w.projectHost.docSource.currentSource = provSource;
    w.projectHost.docSource.currentResource = provResource;
    w.projectHost.activeProjectId.clear();  // a fresh URL/video/OS-open load is a new editor
    w.refreshActions();
    w.fitToWindow();
    w.playImageArrival();

    // --layout (path/URL variant) applies now that an image exists; the inline stencil:// layout was handled up top.
    if (!w.projectHost.docSource.pendingLaunchLayout.isEmpty()) {
      const QString src = w.projectHost.docSource.pendingLaunchLayout;
      const bool strict = w.projectHost.docSource.pendingLaunchStrict;
      w.projectHost.docSource.pendingLaunchLayout.clear();
      w.projectHost.docSource.pendingLaunchStrict = false;
      applyLayoutFromSource(src, strict);
    }
    // Persist as a local project (browser parity: the active editor is always saved); createLocalProject writes pathless pixels to the state dir.
    w.projectHost.adoptCanvasAsLocalProject();
  }

  // Quick-crop override from the load-by-URL dialog (browser defaultCropRect(albumOverride) / noCrop). No-op for Auto / no image.
  void SourceOpener::applyQuickCrop() {
    const QuickCropOpts opts = w.projectHost.docSource.pendingCrop;
    w.projectHost.docSource.pendingCrop = {};  // consume regardless of outcome
    if (!w.canvas->hasImage() || opts.mode == QuickCropOpts::Mode::AUTO) return;
    // A freshly loaded image is un-rotated, so the original IS the crop's pixel space.
    const QImage& orig = w.canvas->getOriginalImage();
    const double iw = orig.width();
    const double ih = orig.height();
    if (iw <= 0 || ih <= 0) return;
    if (opts.mode == QuickCropOpts::Mode::NONE) {
      w.canvas->applyCrop({0.0, 0.0, iw, ih}, /*recalc=*/false);  // full frame, uncropped
      return;
    }
    // Reflect the page in the toolbar control, then crop to it in the chosen orientation.
    if (!opts.page.isEmpty()) {
      const int idx = w.units.pageSize->findData(opts.page);
      if (idx >= 0) w.units.pageSize->setCurrentIndex(idx);  // → onPageSizeChanged
    }
    const core::PageSize pg = naturalPageCm(w.pageSizeValue(),
                                            w.settings.customPageWidth,
                                            w.settings.customPageHeight);
    w.canvas->setPageCm(pg.width, pg.height);
    // What the crop stage was left on wins; with nothing dragged the crop centres as before.
    if (opts.rect.width > 0 && opts.rect.height > 0) {
      w.canvas->applyCrop(opts.rect, /*recalc=*/false);
      return;
    }
    const double aspect = core::cropAspect(pg.width, pg.height, opts.album);
    w.canvas->applyCrop(core::centeredCrop(iw, ih, aspect), /*recalc=*/false);
  }

  // Load a layout JSON from a path or http(s) URL through the shared applyLayoutJson() guards.
  void SourceOpener::applyLayoutFromSource(const QString& src, bool strict) {
    auto adopt = [this, src](const QByteArray& bytes) {
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
      if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        w.notify->error("Invalid layout JSON: " + err.errorString());
        return;
      }
      w.dataExport->applyLayoutJson(doc.object());
    };

    const QUrl url = QUrl::fromUserInput(src);
    if (stencil::net::fetchGuard::isWebScheme(url)) {
      // A URL the USER named takes the loose guard (their localhost stays reachable); a link's, the strict one.
      stencil::net::fetchGuard::get(&w, url, strict,
                                    [this, adopt](const QByteArray& b, const QString& e) {
                                      if (e.isEmpty()) adopt(b);
                                      else w.notify->error("Could not fetch --layout: " + e);
                                    });
      return;
    }
    const QString path =
        QFileInfo(src).exists() ? src : url.toLocalFile();
    QFile f(path.isEmpty() ? src : path);
    if (!f.open(QIODevice::ReadOnly)) {
      w.notify->error("Could not read --layout file");
      return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    adopt(bytes);
  }

}  // namespace stencil::gui
