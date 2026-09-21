#include "MainWindow.hpp"
#include "fetchGuard.hpp"
#include "Notifications.hpp"
#include <QComboBox>
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "cropGeometry.hpp"
#include "launchOptions.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "DataExportController.hpp"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QUrl>

// What happens once a launch image has decoded: quick crop and the layout overlay.

namespace stencil::gui {

  // Adopt a resolved --src image: a local file keeps its path, a remote image / video frame is adopted in-memory. Page aspect first, as openImage().
  void MainWindow::onLaunchImageLoaded(const QImage& image,
                                       const QString& localPath) {
    // Provenance from loadImageByUrl; consumed once.
    const QString provSource = pendingProvSource;
    const QString provResource = pendingProvResource;
    pendingProvSource.clear();
    pendingProvResource.clear();

    // Inline full-layout hand-off (browser→desktop "Open in…"): crop + rotation + filter + lines + page in the ORIGINAL
    // image's space, adopted like a server project — NOT the auto-crop + lines-only import.
    if (!pendingLaunchLayoutJson.isEmpty()) {
      const QString json = pendingLaunchLayoutJson;
      pendingLaunchLayoutJson.clear();
      pendingLaunchLayout.clear();   // an inline layout supersedes any --layout source
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
      if (err.error == QJsonParseError::NoError && doc.isObject() && !image.isNull()) {
        loadImageWithLayout(image, doc.object());
        currentSource = provSource;
        currentResource = provResource;
        refreshActions();
        fitToWindow();
        return;
      }
      notify->error("Invalid layout in the stencil:// link"
                     + (err.error != QJsonParseError::NoError ? QStringLiteral(": ") + err.errorString()
                                                              : QString()));
    }

    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings.customPageWidth,
                                              settings.customPageHeight);
    canvas->setPageCm(page.width, page.height);
    bool ok = false;
    if (!localPath.isEmpty())
      ok = canvas->loadImage(localPath, image);  // path-backed (keeps it for saves)
    if (ok) {
      retainSourceFromFile(localPath);   // lossless .stencil bundle from the untouched file
    } else {
      if (image.isNull()) {
        notify->error("Failed to open the image");
        pendingLaunchLayout.clear();
        return;
      }
      canvas->loadFromImage(image);  // remote image / video frame (in-memory)
      setSourceBytes({}, {});         // in-memory frame/remote → re-encode on bundle
    }
    // Quick pre-load crop from the links modal overrides the page-aspect auto-crop; consumed once.
    applyQuickCrop();
    // A new image's provenance replaces the previous one.
    currentSource = provSource;
    currentResource = provResource;
    activeProjectId.clear();  // a fresh URL/video/OS-open load is a new editor
    refreshActions();
    fitToWindow();
    playImageArrival();

    // --layout (path/URL variant) applies now that an image exists; the inline stencil:// layout was handled up top.
    if (!pendingLaunchLayout.isEmpty()) {
      const QString src = pendingLaunchLayout;
      pendingLaunchLayout.clear();
      applyLayoutFromSource(src);
    }
    // Persist as a local project (browser parity: the active editor is always saved); createLocalProject writes pathless pixels to the state dir.
    adoptCanvasAsLocalProject();
  }

  // Quick-crop override from the load-by-URL dialog (browser defaultCropRect(albumOverride) / noCrop). No-op for Auto / no image.
  void MainWindow::applyQuickCrop() {
    const QuickCropOpts opts = pendingCrop;
    pendingCrop = {};  // consume regardless of outcome
    if (!canvas->hasImage() || opts.mode == QuickCropOpts::Mode::AUTO) return;
    // A freshly loaded image is un-rotated, so the original IS the crop's pixel space.
    const QImage& orig = canvas->getOriginalImage();
    const double iw = orig.width();
    const double ih = orig.height();
    if (iw <= 0 || ih <= 0) return;
    if (opts.mode == QuickCropOpts::Mode::NONE) {
      canvas->applyCrop({0.0, 0.0, iw, ih}, /*recalc=*/false);  // full frame, uncropped
      return;
    }
    // Reflect the page in the toolbar control, then crop to it in the chosen orientation.
    if (!opts.page.isEmpty()) {
      const int idx = units.pageSize->findData(opts.page);
      if (idx >= 0) units.pageSize->setCurrentIndex(idx);  // → onPageSizeChanged
    }
    const core::PageSize pg = naturalPageCm(pageSizeValue(),
                                            settings.customPageWidth,
                                            settings.customPageHeight);
    canvas->setPageCm(pg.width, pg.height);
    // What the crop stage was left on wins; with nothing dragged the crop centres as before.
    if (opts.rect.width > 0 && opts.rect.height > 0) {
      canvas->applyCrop(opts.rect, /*recalc=*/false);
      return;
    }
    const double aspect = core::cropAspect(pg.width, pg.height, opts.album);
    canvas->applyCrop(core::centeredCrop(iw, ih, aspect), /*recalc=*/false);
  }

  // Load a layout JSON from a path or http(s) URL through the shared applyLayoutJson() guards.
  void MainWindow::applyLayoutFromSource(const QString& src) {
    auto adopt = [this, src](const QByteArray& bytes) {
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
      if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        notify->error("Invalid layout JSON: " + err.errorString());
        return;
      }
      dataExport->applyLayoutJson(doc.object());
    };

    const QUrl url = QUrl::fromUserInput(src);
    if (url.scheme() == "http" || url.scheme() == "https") {
      // A URL the USER named, so the loose guard: their localhost stays reachable, internal ranges do not.
      stencil::net::fetchGuard::get(this, url, /*strict=*/false,
                                    [this, adopt](const QByteArray& b, const QString& e) {
                                      if (e.isEmpty()) adopt(b);
                                      else notify->error("Could not fetch --layout: " + e);
                                    });
      return;
    }
    const QString path =
        QFileInfo(src).exists() ? src : url.toLocalFile();
    QFile f(path.isEmpty() ? src : path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify->error("Could not read --layout file");
      return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    adopt(bytes);
  }

}  // namespace stencil::gui
