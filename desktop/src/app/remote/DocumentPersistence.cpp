#include "MainWindow.hpp"
#include "../../support/rowWork.hpp"
#include "DocumentPersistence.hpp"
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"

// Session and per-project view persistence, and the server auto-connect on boot.

namespace stencil::gui {

  // Incognito covers the incognito editor's own state (a desktop-only widening of the browser
  // rule), never other projects.
  SessionController::Gates DocumentPersistence::sessionGates() const {
    return {w.incognito,
            !w.remote.session->getLink().address.isEmpty() && !w.settings.syncToServer,
            !w.activeProjectId.isEmpty(),
            w.canvas && w.canvas->hasImage()};
  }

  // The picture decodes on the pool and lands as it was left, with no arrival; the page and draw
  // mode are set at once. A first show that came meanwhile waits for it (showEvent).
  void DocumentPersistence::restoreSession() {
    auto sess = fileStore::loadSession();
    if (!sess) return;
    if (sess->lines.empty() && sess->imagePath.isEmpty()) return;
    {
      QSignalBlocker b(w.units.pageSize);
      const int idx = w.units.pageSize->findData(sess->pageSize);
      if (idx >= 0) w.units.pageSize->setCurrentIndex(idx);
    }
    // The filter/tint never carries over a relaunch; saveSessionNow still writes it and .stencil
    // files round-trip it.
    w.canvas->setDrawMode(sess->drawMode == "rect"
                             ? CanvasWidget::DrawMode::RECT
                             : CanvasWidget::DrawMode::LINE);
    const auto reveal = [this] {
      if (!w.docSource.revealHeld) return;
      w.docSource.revealHeld = false;
      fadeInWindow(&w);
    };
    const Session s = *sess;
    const auto land = [this, s, reveal](const QImage& decoded) {
      const core::PageSize page = naturalPageCm(s.pageSize, s.customPageWidth, s.customPageHeight);
      w.canvas->setPageCm(page.width, page.height);
      w.canvas->restore(s.imagePath, s.lines, s.scale, s.cropRect, s.rotationQuarters, decoded);
      // Re-bind so removing that project empties the editor instead of orphaning its picture.
      if (!s.activeProjectId.isEmpty() && w.findProject(s.activeProjectId.toStdString()))
        w.activeProjectId = s.activeProjectId;
      // Guarded: a restoring setZoom would re-persist itself and pop a stray "Saved" toast on
      // launch.
      w.session.setRestoring(true);
      w.setZoom(s.scale);
      w.session.setRestoring(false);
      w.refreshActions();
      reveal();
    };
    if (s.imagePath.isEmpty()) return land(QImage());
    const QString path = s.imagePath;
    w.decodeForCanvas([path] { return QImage(path); }, land, reveal);
    w.docSource.sessionLoad = w.canvas->pictureGeneration();
  }

  // Until the boot restore lands, the session on disk is the one it is restoring.
  bool DocumentPersistence::sessionRestorePending() const {
    return w.docSource.sessionLoad != 0 && w.canvas->pictureGeneration() == w.docSource.sessionLoad;
  }

  // Browser parity: storage.js's debounced scroll/zoom listener — a drag-pan ends in one save, not
  // one per pixel.
  void DocumentPersistence::scheduleViewSave() { w.session.scheduleViewSave(sessionGates()); }

  // Touches only the view fields and never bumps the Dock "recent" list.
  void DocumentPersistence::saveActiveProjectView() {
    if (!SessionController::wantsViewWrite(sessionGates(), w.session.getRestoring())) return;
    Project* pr = w.findProject(w.activeProjectId.toStdString());
    if (!pr || !w.canvas || !w.scroll) return;
    const double zoom = w.canvas->getScale();
    const int left = w.scroll->horizontalScrollBar()->value();
    const int top = w.scroll->verticalScrollBar()->value();
    if (!SessionController::viewMoved(zoom, left, top, pr->zoomScale, pr->scrollLeft,
                                      pr->scrollTop))
      return;
    pr->zoomScale = zoom;
    pr->scrollLeft = left;
    pr->scrollTop = top;
    fileStore::saveProjects(w.projectList);
    // Browser parity: storage.save() flashes "Saved" on this path too.
    w.notify->success(QStringLiteral("Saved"));
  }

  void DocumentPersistence::autoConnectServers() {
    if (!stencil::net::connectionStore::getAutoConnect()) return;
    const QVector<stencil::net::SavedServer> saved =
        stencil::net::connectionStore::loadSavedServers();
    if (saved.isEmpty()) return;
    stencil::net::ConnectionManager* mgr = w.ensureConnections();
    auto left = std::make_shared<int>(saved.size());
    for (const auto& srv : saved) {
      // A proven admin credential mints straight away instead of a doomed /projects probe.
      mgr->connectToAsync(
          srv.url, srv.token,
          [this, url = srv.url, left](bool ok, QString) {
            // One toast per address: a count says nothing about which server to check.
            if (!ok) w.notify->info(QString("Couldn't reach %1").arg(url));
            if (--*left == 0) w.warnInsecureConnections();
          },
          stencil::net::ServerClient::kindFromTag(srv.kind));
    }
  }

  // The canvas is copied here; the full-size render and its PNG encode run on the pool.
  void DocumentPersistence::uploadServerResult(std::function<void()> done) {
    const QString addr = w.remote.session->getLink().address;
    const QString id = w.remote.session->getLink().id;
    if (!w.remote.connections || !w.remote.connections->find(addr) || !w.canvas->hasImage() || !w.settings.syncToServer) {
      done();
      return;
    }
    const int width = w.canvas->imageWidth();
    const int height = w.canvas->imageHeight();
    const std::shared_ptr<CanvasScene> scene = w.canvas->renderCopy();
    QPointer<MainWindow> self(&w);
    support::runOnPool<QByteArray>(
        &w, [scene] { return pngBytes(scene->renderToImage(true)); },
        [this, self, addr, id, width, height, done](QByteArray bytes) {
          stencil::net::ServerClient* c = w.remote.connections ? w.remote.connections->find(addr) : nullptr;
          if (!c) { done(); return; }
          const qint64 before = w.remote.session->getLink().version;
          c->uploadFileAsync(id, "result", bytes, "png", width, height, [this, self, c, addr, id, before, done](bool ok) {
            if (!self || !ok) { done(); return; }
            c->getProjectAsync(id, [this, self, addr, id, before, done](bool gok, stencil::net::ServerProject meta,
                                                                        QJsonObject) {
              if (self && gok && w.remote.session->getLink().address == addr &&
                  w.remote.session->getLink().id == id)
                adoptOwnFileVersion(w.remote.session->getLink(), before, meta.version);
              done();
            });
          });
        });
  }
}  // namespace stencil::gui
