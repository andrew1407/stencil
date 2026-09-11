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

  // See buildProjectThumbs() in the header. The active project renders from the live
  // canvas (current, possibly unsaved edits); the rest composite offscreen from their
  // stored image+crop+rotation+lines. A pathless source has no pixels to reload.
  QHash<QString, QPixmap> MainWindow::buildProjectThumbs() const {
    QHash<QString, QPixmap> out;
    // Rendered larger than the 56px row icon so the dialog's hover-magnify preview
    // stays crisp; the list downscales it for the icon column via setIconSize.
    constexpr int kThumb = 320;
    const bool dark = resolveDark(settings_.themeMode);
    // One reusable offscreen renderer (never shown), themed + flagged to match the
    // editor so the previews look like what the user would see on open.
    CanvasWidget off;
    off.setDark(dark);
    off.setAccent(settings_.accentColor);
    off.setShowPoints(settings_.showPoints);
    off.setShowLines(settings_.showLines);
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    off.setPageCm(page.width, page.height);
    // Decoding every stored source is the bulk of this and is pure CPU, so the whole
    // registry is decoded on the pool first. Only the decode moves: QPixmap and the
    // offscreen CanvasWidget are main-thread only.
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
        // Local projects don't persist a per-project filter; the canvas applies the
        // global filter on open, so the preview uses it too (what you'd see on open).
        off.setImageFilter(settings_.imageFilter, filterColorValue_);
        rendered = off.renderToImage(/*withOverlay=*/true);
      }
      if (rendered.isNull()) continue;
      out.insert(id, QPixmap::fromImage(rendered.scaled(
                         kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
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
    // Auto-refresh on open: restart the expiry window when enabled (mirrors the
    // browser storage.loadProject snap). Keep-forever (expiresAt 0) is untouched.
    if (pr->meta.autoRefresh && pr->meta.expiresAt != 0) {
      pr->meta.expiresAt = core::ProjectsStore::addPeriod(nowMs(), pr->meta.refreshPeriod);
      fileStore::saveProjects(projectList_);
    }
    activeProjectId_ = id;
    remoteSession_->link().unbind();  // a local project is not server-linked
    remoteSync_->stopRemotePoll();   // no longer a server session
    currentSource_ = QString::fromStdString(pr->meta.source);
    currentResource_ = QString::fromStdString(pr->meta.resource);
    // Restore the blank-fill colour so the Blank control reappears for a reopened blank.
    blankColor_ = pr->meta.blank ? QString::fromStdString(pr->meta.blankColor) : QString();
    canvas_->setBlankPage(!blankColor_.isEmpty());
    // Chat persistence (§12): with saving on the conversation is project-scoped —
    // swap in this project's saved chat (an absent one = a fresh scope). With it
    // off, the session conversation survives switches (the pre-§12 behavior).
    if (settings_.saveChatsWithProject) restoreChatFromDoc(pr->chat);
    refreshActions();
    // Restore this project's own pan/zoom if it saved one (browser parity: storage.js's
    // `if (layout.zoom) { setZoom(...); restore scroll } else fitToWindow()`) — otherwise
    // fall back to the plain fit. Guarded so restoring the saved values doesn't immediately
    // re-schedule (and re-persist) a save of what was just read back.
    restoringView_ = true;
    if (pr->zoomScale > 0) {
      setZoom(pr->zoomScale);
      // Deferred a turn (like the browser's requestAnimationFrame): the scrollbars' range
      // reflects the new zoom only after this resize's layout pass has actually run.
      const int sx = pr->scrollLeft, sy = pr->scrollTop;
      QTimer::singleShot(0, this, [this, sx, sy] {
        if (scroll_) {
          scroll_->horizontalScrollBar()->setValue(sx);
          scroll_->verticalScrollBar()->setValue(sy);
        }
        restoringView_ = false;
      });
    } else {
      fitToWindow();   // fit the opened project to the window (matches the browser)
      restoringView_ = false;
    }
    if (animate) playImageArrival();   // a reopened project's picture APPEARS, like any other
    notify_->success(
        QString("Opened \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
    return true;
  }

  // Adopt a full layout envelope onto `img` (crop + rotation + filter + lines +
  // page/formulas). Shared by openServerProject and the inline browser→desktop
  // "Open in…" hand-off so both restore the exact session — not just the lines.
  void MainWindow::loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                                       const QByteArray& sourceBytes, const QString& sourceExt) {
    // Retain the untouched source bytes for a lossless .stencil re-bundle (empty ⇒ re-encode).
    setSourceBytes(sourceBytes, sourceExt);
    // Adopt the page format + formulas before sizing the canvas page below.
    adoptServerLayoutMeta(layout);
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    // Restore geometry (rotation + crop) from the layout, then adopt the lines. Rotation
    // applies before the crop (the crop lives in rotated-original space); an empty/old
    // layout default-crops and stays un-rotated.
    int lw = 0, lh = 0;
    core::CropRect crop;
    int rot = 0;
    core::Lines lines = fileStore::parseLayoutJson(layout, lw, lh, &crop, &rot);
    canvas_->loadFromImage(img, crop, rot);
    if (!lines.empty()) canvas_->setLines(lines);
    // Restore the saved filter/tint (an empty layout resets to "none" + the default tint,
    // so a prior image's filter — or the desktop's default filter — doesn't bleed in).
    QString filter, tint;
    parseLayoutFilter(layout, settings_.filterColor, filter, tint);
    applyTintColor(QColor(tint));
    applyImageFilter(filter);
  }

}  // namespace stencil::gui
