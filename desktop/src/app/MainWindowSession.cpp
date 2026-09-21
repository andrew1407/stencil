#include "MainWindow.hpp"
#include <QComboBox>
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "OverlayScrollArea.hpp"
#include "CropDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "connectionStore.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include "ServerClient.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/iconMotion.hpp"

#include <QSignalBlocker>

// Session and per-project view persistence, and the server auto-connect on boot.

namespace stencil::gui {

  // Incognito covers the incognito editor's own state (a desktop-only widening of the browser
  // rule), never other projects.
  SessionController::Gates MainWindow::sessionGates() const {
    return {incognito,
            !remoteSession->getLink().address.isEmpty() && !settings.syncToServer,
            !activeProjectId.isEmpty(),
            canvas && canvas->hasImage()};
  }

  void MainWindow::scheduleAutosave() {
    session.scheduleAutosave(settings.autosave, sessionGates());
  }

  void MainWindow::saveSessionNow() {
    if (!SessionController::wantsSessionWrite(sessionGates())) return;
    Session s;
    s.imagePath = canvas->getImagePath();
    s.pageSize = pageSizeValue();
    s.scale = canvas->getScale();
    s.lines = canvas->allLines();
    s.customPageWidth = settings.customPageWidth;
    s.customPageHeight = settings.customPageHeight;
    // Filter / tint / draw mode ride along in the layout blob (browser storage.js:40-41,54).
    s.imageFilter = settings.imageFilter;
    s.filterColor = settings.filterColor;
    s.drawMode =
        canvas->getDrawMode() == CanvasWidget::DrawMode::RECT ? "rect" : "line";
    s.cropRect = canvas->getCropRect();
    s.rotationQuarters = canvas->getRotationQuarters();
    s.activeProjectId = activeProjectId;
    fileStore::saveSession(s);
  }

  void MainWindow::restoreSession() {
    auto sess = fileStore::loadSession();
    if (!sess) return;
    if (sess->lines.empty() && sess->imagePath.isEmpty()) return;
    {
      const core::PageSize page = naturalPageCm(
          sess->pageSize, sess->customPageWidth, sess->customPageHeight);
      canvas->setPageCm(page.width, page.height);
    }
    canvas->restore(sess->imagePath, sess->lines, sess->scale, sess->cropRect,
                     sess->rotationQuarters);
    // Re-bind so removing that project empties the editor instead of orphaning its picture.
    if (!sess->activeProjectId.isEmpty()
        && findProject(sess->activeProjectId.toStdString())) {
      activeProjectId = sess->activeProjectId;
    }
    {
      QSignalBlocker b(units.pageSize);
      const int idx = units.pageSize->findData(sess->pageSize);
      if (idx >= 0) units.pageSize->setCurrentIndex(idx);
    }
    // The filter/tint never carries over a relaunch; saveSessionNow still writes it and .stencil
    // files round-trip it.
    canvas->setDrawMode(sess->drawMode == "rect"
                             ? CanvasWidget::DrawMode::RECT
                             : CanvasWidget::DrawMode::LINE);
    // Guarded: a restoring setZoom would re-persist itself and pop a stray "Saved" toast on
    // launch.
    session.setRestoring(true);
    setZoom(sess->scale);
    session.setRestoring(false);
  }

  // Browser parity: storage.js's debounced scroll/zoom listener — a drag-pan ends in one save, not
  // one per pixel.
  void MainWindow::scheduleViewSave() { session.scheduleViewSave(sessionGates()); }

  // Touches only the view fields and never bumps the Dock "recent" list.
  void MainWindow::saveActiveProjectView() {
    if (!SessionController::wantsViewWrite(sessionGates(), session.getRestoring())) return;
    Project* pr = findProject(activeProjectId.toStdString());
    if (!pr || !canvas || !scroll) return;
    const double zoom = canvas->getScale();
    const int left = scroll->horizontalScrollBar()->value();
    const int top = scroll->verticalScrollBar()->value();
    if (!SessionController::viewMoved(zoom, left, top, pr->zoomScale, pr->scrollLeft,
                                      pr->scrollTop))
      return;
    pr->zoomScale = zoom;
    pr->scrollLeft = left;
    pr->scrollTop = top;
    fileStore::saveProjects(projectList);
    // Browser parity: storage.save() flashes "Saved" on this path too.
    notify->success(QStringLiteral("Saved"));
  }

  stencil::net::ConnectionManager* MainWindow::ensureConnections() {
    if (!connections) {
      connections = new stencil::net::ConnectionManager(this);
      // The desktop analogue of the browser connectionManager onChange → saveServers.
      connect(connections, &stencil::net::ConnectionManager::changed, this, [this] {
        stencil::net::connectionStore::saveServers(connections->snapshot());
      });
      // The manager starts null until this lazy creation.
      remoteSession->setConnections(connections);
    }
    return connections;
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
            if (!ok) notify->info(QString("Couldn't reach %1").arg(url));
            if (--*left == 0) warnInsecureConnections();
          },
          stencil::net::ServerClient::kindFromTag(srv.kind));
    }
  }

  void MainWindow::warnInsecureConnections() {
    if (!connections) return;
    QStringList insecure;
    for (auto* c : connections->getClients())
      if (stencil::net::ServerClient::isInsecureRemote(c->getBase())) insecure << c->getBase();
    if (insecure.isEmpty()) return;
    notify->error(
        QString("Insecure connection: %1 uses plaintext http — your access token and "
                "images are sent unencrypted. Use https on untrusted networks.")
            .arg(insecure.join(", ")));
  }

}  // namespace stencil::gui
