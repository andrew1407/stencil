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

  // Incognito covers the incognito editor's own state (a desktop-only widening of the browser
  // rule), never other projects.
  SessionController::Gates MainWindow::sessionGates() const {
    return {incognito_,
            !remoteSession_->link().address.isEmpty() && !settings_.syncToServer,
            !activeProjectId_.isEmpty(),
            canvas_ && canvas_->hasImage()};
  }

  void MainWindow::scheduleAutosave() {
    session_.scheduleAutosave(settings_.autosave, sessionGates());
  }

  void MainWindow::saveSessionNow() {
    if (!SessionController::wantsSessionWrite(sessionGates())) return;
    Session s;
    s.imagePath = canvas_->imagePath();
    s.pageSize = pageSizeValue();
    s.scale = canvas_->scale();
    s.lines = canvas_->allLines();
    s.customPageWidth = settings_.customPageWidth;
    s.customPageHeight = settings_.customPageHeight;
    // Filter / tint / draw mode ride along in the layout blob (browser storage.js:40-41,54).
    s.imageFilter = settings_.imageFilter;
    s.filterColor = settings_.filterColor;
    s.drawMode =
        canvas_->drawMode() == CanvasWidget::DrawMode::RECT ? "rect" : "line";
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
    // Re-bind so removing that project empties the editor instead of orphaning its picture.
    if (!sess->activeProjectId.isEmpty()
        && findProject(sess->activeProjectId.toStdString())) {
      activeProjectId_ = sess->activeProjectId;
    }
    {
      QSignalBlocker b(units_.pageSize);
      const int idx = units_.pageSize->findData(sess->pageSize);
      if (idx >= 0) units_.pageSize->setCurrentIndex(idx);
    }
    // The filter/tint never carries over a relaunch; saveSessionNow still writes it and .stencil
    // files round-trip it.
    canvas_->setDrawMode(sess->drawMode == "rect"
                             ? CanvasWidget::DrawMode::RECT
                             : CanvasWidget::DrawMode::LINE);
    // Guarded: a restoring setZoom would re-persist itself and pop a stray "Saved" toast on
    // launch.
    session_.setRestoring(true);
    setZoom(sess->scale);
    session_.setRestoring(false);
  }

  // Browser parity: storage.js's debounced scroll/zoom listener — a drag-pan ends in one save, not
  // one per pixel.
  void MainWindow::scheduleViewSave() { session_.scheduleViewSave(sessionGates()); }

  // Touches only the view fields and never bumps the Dock "recent" list.
  void MainWindow::saveActiveProjectView() {
    if (!SessionController::wantsViewWrite(sessionGates(), session_.restoring())) return;
    Project* pr = findProject(activeProjectId_.toStdString());
    if (!pr || !canvas_ || !scroll_) return;
    const double zoom = canvas_->scale();
    const int left = scroll_->horizontalScrollBar()->value();
    const int top = scroll_->verticalScrollBar()->value();
    if (!SessionController::viewMoved(zoom, left, top, pr->zoomScale, pr->scrollLeft,
                                      pr->scrollTop))
      return;
    pr->zoomScale = zoom;
    pr->scrollLeft = left;
    pr->scrollTop = top;
    fileStore::saveProjects(projectList_);
    // Browser parity: storage.save() flashes "Saved" on this path too.
    notify_->success(QStringLiteral("Saved"));
  }

  stencil::net::ConnectionManager* MainWindow::ensureConnections() {
    if (!connections_) {
      connections_ = new stencil::net::ConnectionManager(this);
      // The desktop analogue of the browser connectionManager onChange → saveServers.
      connect(connections_, &stencil::net::ConnectionManager::changed, this, [this] {
        stencil::net::connectionStore::saveServers(connections_->snapshot());
      });
      // The manager starts null until this lazy creation.
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
    auto left = std::make_shared<int>(saved.size());
    for (const auto& srv : saved) {
      // A proven admin credential mints straight away instead of a doomed /projects probe.
      mgr->connectToAsync(
          srv.url, srv.token,
          [this, url = srv.url, left](bool ok, QString) {
            // One toast per address: a count says nothing about which server to check.
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
