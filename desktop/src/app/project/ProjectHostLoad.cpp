#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "ChatSessionController.hpp"
#include "mainWindowShared.hpp"
#include "naturalPage.hpp"
#include "stamps.hpp"
#include "displayName.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"

#include <QImage>
#include <QJsonObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

// Loading a local project onto the canvas.

namespace stencil::gui {

  bool ProjectHost::loadProjectIntoCanvas(const QString& id, bool animate,
                                         std::function<void(bool)> landed) {
    const Project* found = findProject(id.toStdString());
    if (!found) return false;
    const QString path = found->imagePath;
    // A failed decode keeps the old picture under the project's lines, as a failed read always has.
    w.decodeForCanvas(
        [path] { return path.isEmpty() ? QImage() : QImage(path); },
        [this, id, animate, landed](const QImage& decoded) {
          Project* pr = findProject(id.toStdString());
          if (!pr) {   // removed while its picture decoded
            if (landed) landed(false);
            return;
          }
          {
            const core::PageSize page = naturalPageCm(w.pageSizeValue(),
                                                      w.settings.customPageWidth,
                                                      w.settings.customPageHeight);
            w.canvas->setPageCm(page.width, page.height);
          }
          w.canvas->restore(pr->imagePath, pr->lines, w.canvas->getScale(), pr->cropRect,
                           pr->rotationQuarters, decoded, pr->mirrored);
          // Mirrors the browser storage.loadProject snap; keep-forever (expiresAt 0) is untouched.
          if (pr->meta.autoRefresh && pr->meta.expiresAt != 0) {
            pr->meta.expiresAt = core::ProjectsStore::addPeriod(nowMs(), pr->meta.refreshPeriod);
            SharedState::instance().saveProjects(w.window);
          }
          activeProjectId = id;
          fileStore::markProjectOpened(id, nowMs());   // the projects list's "Recently opened"
          w.remote.session->getLink().unbind();  // a local project is not server-linked
          w.remoteSync->stopRemotePoll();   // no longer a server session
          docSource.currentSource = QString::fromStdString(pr->meta.source);
          docSource.currentResource = QString::fromStdString(pr->meta.resource);
          docSource.blankColor = pr->meta.blank ? QString::fromStdString(pr->meta.blankColor) : QString();
          w.canvas->setBlankPage(!docSource.blankColor.isEmpty());
          // Chat persistence (§12): with saving on the conversation is project-scoped; off, it survives
          // switches.
          if (w.settings.saveChatsWithProject) w.chatSession->restoreChatFromDoc(pr->chat);
          w.refreshActions();
          // Browser parity: storage.js restores `layout.zoom` + scroll, else fitToWindow(). Guarded so
          // the restore does not re-persist itself.
          w.session.setRestoring(true);
          if (pr->zoomScale > 0) {
            w.setZoom(pr->zoomScale);
            // Deferred a turn (browser requestAnimationFrame): the scrollbar range reflects the zoom
            // only after the layout pass.
            const int sx = pr->scrollLeft, sy = pr->scrollTop;
            QTimer::singleShot(0, w.window, [this, sx, sy] {
              if (w.scroll) {
                w.scroll->horizontalScrollBar()->setValue(sx);
                w.scroll->verticalScrollBar()->setValue(sy);
              }
              w.session.setRestoring(false);
            });
          } else {
            w.fitToWindow();   // fit the opened project to the window (matches the browser)
            w.session.setRestoring(false);
          }
          if (animate) w.playImageArrival();   // a reopened project's picture APPEARS, like any other
          w.notify->success(
              QString("Opened \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
          if (landed) landed(true);
        },
        [landed] { if (landed) landed(false); });
    return true;
  }
}  // namespace stencil::gui
