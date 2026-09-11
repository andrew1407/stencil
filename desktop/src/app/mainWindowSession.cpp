#include "mainWindow.hpp"
#include <QComboBox>
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "overlayScrollArea.hpp"
#include "cropDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "connectionStore.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "projectTransferController.hpp"
#include "serverClient.hpp"
#include "shortcutsDialog.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/iconMotion.hpp"

#include <QSignalBlocker>

// Session and per-project view persistence, and the server auto-connect on boot.

namespace stencil::gui {

  // Incognito gates every write of the incognito editor's OWN state (session,
  // settings, promotion, shortcut overrides — a deliberate desktop-only widening
  // of the browser rule) but never maintenance on OTHER saved projects.
  void MainWindow::scheduleAutosave() {
    if (incognito_) return;  // no autosave timer while incognito
    if (settings_.autosave) autosaveTimer_->start(600);
  }

  void MainWindow::saveSessionNow() {
    if (incognito_) return;  // skip session writes while incognito
    // Sync off + a fetched server project = edit-in-memory only: don't persist the
    // restore blob either (the session is "stored nowhere").
    if (!remoteSession_->link().address.isEmpty() && !settings_.syncToServer) return;
    Session s;
    s.imagePath = canvas_->imagePath();
    s.pageSize = pageSizeValue();
    s.scale = canvas_->scale();
    s.lines = canvas_->allLines();
    s.customPageWidth = settings_.customPageWidth;
    s.customPageHeight = settings_.customPageHeight;
    // Image filter / tint / draw mode ride along in the layout blob (browser
    // storage.js:40-41,54). drawMode mirrors the canvas, the filter/tint mirror
    // Settings (Step 3 applies them to the canvas).
    s.imageFilter = settings_.imageFilter;
    s.filterColor = settings_.filterColor;
    s.drawMode =
        canvas_->drawMode() == CanvasWidget::DrawMode::Rect ? "rect" : "line";
    s.cropRect = canvas_->cropRect();
    s.rotationQuarters = canvas_->rotationQuarters();
    s.activeProjectId = activeProjectId_;
    fileStore::saveSession(s);
  }

  void MainWindow::restoreSession() {
    auto sess = fileStore::loadSession();
    if (!sess) return;
    if (sess->lines.empty() && sess->imagePath.isEmpty()) return;
    {
      const core::PageSize page = naturalPageCm(
          sess->pageSize, sess->customPageWidth, sess->customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    canvas_->restore(sess->imagePath, sess->lines, sess->scale, sess->cropRect,
                     sess->rotationQuarters);
    // Re-bind the restored canvas to its project (when it still exists), so removing
    // that project empties the editor instead of orphaning its picture on screen.
    if (!sess->activeProjectId.isEmpty()
        && findProject(sess->activeProjectId.toStdString())) {
      activeProjectId_ = sess->activeProjectId;
    }
    {
      QSignalBlocker b(pageSize_);
      const int idx = pageSize_->findData(sess->pageSize);
      if (idx >= 0) pageSize_->setCurrentIndex(idx);
    }
    // The filter/tint deliberately does NOT carry over into a relaunch (user decision);
    // saveSessionNow still writes it (format unchanged) and .stencil files round-trip it.
    canvas_->setDrawMode(sess->drawMode == "rect"
                             ? CanvasWidget::DrawMode::Rect
                             : CanvasWidget::DrawMode::Line);
    // Guarded: this setZoom is restoring a value, not the user zooming — without the guard
    // it would immediately re-schedule (and, once the debounce fires, re-persist) the exact
    // zoom that was just read back, and could pop a stray "Saved" toast right on launch.
    restoringView_ = true;
    setZoom(sess->scale);
    restoringView_ = false;
  }

  // Persist the active project's pan/zoom position only (browser parity: storage.js's
  // scrollLeft/scrollTop/zoom, saved via a debounced listener) — every route that moves the
  // view (setZoom, both scrollbars) calls this instead of saving directly, so a drag-pan or
  // a zoom burst ends in ONE save/toast, not one per pixel/step.
  void MainWindow::scheduleViewSave() {
    if (incognito_ || activeProjectId_.isEmpty() || restoringView_) return;
    if (!canvas_ || !canvas_->hasImage()) return;
    viewSaveTimer_->start(400);   // browser parity: storage.js's own scroll-save debounce
  }

  // What scheduleViewSave's debounce actually runs. Lighter than saveToActiveProject(): it
  // touches only the view fields, not lines/crop/chat, and doesn't bump the Dock "recent"
  // list — a pan/zoom is not the kind of change that belongs in either.
  void MainWindow::saveActiveProjectView() {
    if (incognito_ || activeProjectId_.isEmpty() || restoringView_) return;
    // Edit-in-memory-only states (mirrors saveToActiveProject's own gates): nothing local
    // to write back to.
    if (!remoteSession_->link().address.isEmpty() && !settings_.syncToServer) return;
    Project* pr = findProject(activeProjectId_.toStdString());
    if (!pr || !canvas_ || !scroll_) return;
    const double zoom = canvas_->scale();
    const int left = scroll_->horizontalScrollBar()->value();
    const int top = scroll_->verticalScrollBar()->value();
    // Nothing actually moved (a layout-induced scrollbar valueChanged — resize, panel
    // fold — also trips the debounce): don't rewrite the store or toast "Saved".
    if (pr->zoomScale == zoom && pr->scrollLeft == left && pr->scrollTop == top) return;
    pr->zoomScale = zoom;
    pr->scrollLeft = left;
    pr->scrollTop = top;
    fileStore::saveProjects(projectList_);
    // Browser parity: storage.save() flashes "Saved" whenever a project persists —
    // including this debounced pan/zoom path (storage.js's own scroll/zoom listeners).
    notify_->success(QStringLiteral("Saved"));
  }

  stencil::net::ConnectionManager* MainWindow::ensureConnections() {
    if (!connections_) {
      connections_ = new stencil::net::ConnectionManager(this);
      // Persist the live set on every change (connect / disconnect / reconnect) so
      // it survives relaunch — the desktop analogue of the browser connectionManager
      // onChange → saveServers.
      connect(connections_, &stencil::net::ConnectionManager::changed, this, [this] {
        stencil::net::connectionStore::saveServers(connections_->snapshot());
      });
      // Hand the manager to the session so RemoteSession::requireClient + the sync controller
      // resolve clients through it (it starts null until this lazy creation).
      remoteSession_->setConnections(connections_);
    }
    return connections_;
  }

  void MainWindow::autoConnectServers() {
    if (!stencil::net::connectionStore::getAutoConnect()) return;
    const QVector<stencil::net::SavedServer> saved =
        stencil::net::connectionStore::loadSavedServers();
    if (saved.isEmpty()) return;
    stencil::net::ConnectionManager* mgr = ensureConnections();
    // All at once, none of them blocking the window: the warning waits for the last.
    auto left = std::make_shared<int>(saved.size());
    for (const auto& srv : saved) {
      // The saved kind rides along: a proven admin credential mints straight away
      // instead of spending a doomed /projects probe on it first.
      mgr->connectToAsync(
          srv.url, srv.token,
          [this, url = srv.url, left](bool ok, QString) {
            // One toast per address, not a count — a count says nothing about WHICH
            // server to go check (a dead server stays absent from the live set).
            if (!ok) notify_->info(QString("Couldn't reach %1").arg(url));
            if (--*left == 0) warnInsecureConnections();
          },
          stencil::net::ServerClient::kindFromTag(srv.kind));
    }
  }

  void MainWindow::warnInsecureConnections() {
    if (!connections_) return;
    QStringList insecure;
    for (auto* c : connections_->clients())
      if (stencil::net::ServerClient::isInsecureRemote(c->base())) insecure << c->base();
    if (insecure.isEmpty()) return;
    notify_->error(
        QString("Insecure connection: %1 uses plaintext http — your access token and "
                "images are sent unencrypted. Use https on untrusted networks.")
            .arg(insecure.join(", ")));
  }

}  // namespace stencil::gui
