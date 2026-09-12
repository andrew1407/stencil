#include "mainWindow.hpp"
#include <QScrollBar>
#include <QScrollArea>
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "displayName.hpp"
#include "openImageDialog.hpp"
#include "../support/rowWork.hpp"
#include "canvasWidget.hpp"
#include "dropZonesOverlay.hpp"
#include "cropGeometry.hpp"
#include "cropDialog.hpp"
#include "guiHelpers.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "projectTransferController.hpp"
#include "liveFeed.hpp"
#include "selectionPanel.hpp"
#include "assistantSettingsDialog.hpp"
#include "settingsDialog.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"
#include "../support/dockGrip.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QImage>
#include <QJsonObject>
#include <QPixmap>
#include <QTimer>

// Loading a local project onto the canvas, plus its thumbnail pass.

namespace stencil::gui {

  // The active project renders from the live canvas; the rest composite offscreen from their
  // stored image+crop+rotation+lines.
  QHash<QString, QPixmap> MainWindow::buildProjectThumbs() const {
    QHash<QString, QPixmap> out;
    // Larger than the 56px row icon so the hover-magnify preview stays crisp.
    constexpr int THUMB = 320;
    const bool dark = resolveDark(settings_.themeMode);
    CanvasWidget off;
    off.setDark(dark);
    off.setAccent(settings_.accentColor);
    off.setShowPoints(settings_.showPoints);
    off.setShowLines(settings_.showLines);
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    off.setPageCm(page.width, page.height);
    // Decoding is the bulk and pure CPU, so it runs on the pool; QPixmap and the offscreen
    // CanvasWidget are main-thread only.
    const int n = static_cast<int>(projectList_.size());
    QVector<QImage> decoded(n);
    support::forEachSlice(n, 1, [&](int i0, int i1) {
      for (int i = i0; i < i1; ++i)
        if (!projectList_[i].imagePath.isEmpty()) decoded[i].load(projectList_[i].imagePath);
    });
    for (int i = 0; i < n; ++i) {
      const auto& pr = projectList_[i];
      const QString id = QString::fromStdString(pr.meta.id);
      QImage rendered;
      if (id == activeProjectId_ && canvas_->hasImage()) {
        rendered = canvas_->renderToImage(/*withOverlay=*/true);  // live edited result
      } else if (!pr.imagePath.isEmpty()) {
        off.restore(pr.imagePath, pr.lines, 1.0, pr.cropRect, pr.rotationQuarters, decoded[i]);
        // Local projects have no per-project filter; the preview applies the global one like open
        // does.
        off.setImageFilter(settings_.imageFilter, filterColorValue_);
        rendered = off.renderToImage(/*withOverlay=*/true);
      }
      if (rendered.isNull()) continue;
      out.insert(id, QPixmap::fromImage(rendered.scaled(
                         THUMB, THUMB, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    return out;
  }

  bool MainWindow::loadProjectIntoCanvas(const QString& id, bool animate) {
    Project* pr = findProject(id.toStdString());
    if (!pr) return false;
    {
      const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    canvas_->restore(pr->imagePath, pr->lines, canvas_->scale(), pr->cropRect,
                     pr->rotationQuarters);
    // Mirrors the browser storage.loadProject snap; keep-forever (expiresAt 0) is untouched.
    if (pr->meta.autoRefresh && pr->meta.expiresAt != 0) {
      pr->meta.expiresAt = core::ProjectsStore::addPeriod(nowMs(), pr->meta.refreshPeriod);
      fileStore::saveProjects(projectList_);
    }
    activeProjectId_ = id;
    remoteSession_->link().unbind();  // a local project is not server-linked
    remoteSync_->stopRemotePoll();   // no longer a server session
    currentSource_ = QString::fromStdString(pr->meta.source);
    currentResource_ = QString::fromStdString(pr->meta.resource);
    blankColor_ = pr->meta.blank ? QString::fromStdString(pr->meta.blankColor) : QString();
    canvas_->setBlankPage(!blankColor_.isEmpty());
    // Chat persistence (§12): with saving on the conversation is project-scoped; off, it survives
    // switches.
    if (settings_.saveChatsWithProject) restoreChatFromDoc(pr->chat);
    refreshActions();
    // Browser parity: storage.js restores `layout.zoom` + scroll, else fitToWindow(). Guarded so
    // the restore does not re-persist itself.
    session_.setRestoring(true);
    if (pr->zoomScale > 0) {
      setZoom(pr->zoomScale);
      // Deferred a turn (browser requestAnimationFrame): the scrollbar range reflects the zoom
      // only after the layout pass.
      const int sx = pr->scrollLeft, sy = pr->scrollTop;
      QTimer::singleShot(0, this, [this, sx, sy] {
        if (scroll_) {
          scroll_->horizontalScrollBar()->setValue(sx);
          scroll_->verticalScrollBar()->setValue(sy);
        }
        session_.setRestoring(false);
      });
    } else {
      fitToWindow();   // fit the opened project to the window (matches the browser)
      session_.setRestoring(false);
    }
    if (animate) playImageArrival();   // a reopened project's picture APPEARS, like any other
    notify_->success(
        QString("Opened \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
    return true;
  }

  // Shared by openServerProject and the "Open in…" hand-off so both restore the exact session.
  void MainWindow::loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                                       const QByteArray& sourceBytes, const QString& sourceExt) {
    // Untouched source bytes for a lossless .stencil re-bundle (empty ⇒ re-encode).
    setSourceBytes(sourceBytes, sourceExt);
    adoptServerLayoutMeta(layout);
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    // Rotation applies before the crop (the crop lives in rotated-original space).
    int lw = 0, lh = 0;
    core::CropRect crop;
    int rot = 0;
    core::Lines lines = fileStore::parseLayoutJson(layout, lw, lh, &crop, &rot);
    canvas_->loadFromImage(img, crop, rot);
    if (!lines.empty()) canvas_->setLines(lines);
    // An empty layout resets to "none" + the default tint, so a prior filter never bleeds in.
    QString filter, tint;
    parseLayoutFilter(layout, settings_.filterColor, filter, tint);
    applyTintColor(QColor(tint));
    applyImageFilter(filter);
  }

}  // namespace stencil::gui
