#include "MainWindow.hpp"
#include "ChatSessionController.hpp"
#include "mainWindowShared.hpp"
#include "mainWindowHelpers.hpp"
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

  bool MainWindow::loadProjectIntoCanvas(const QString& id, bool animate,
                                         std::function<void(bool)> landed) {
    const Project* found = findProject(id.toStdString());
    if (!found) return false;
    const QString path = found->imagePath;
    // A failed decode keeps the old picture under the project's lines, as a failed read always has.
    decodeForCanvas(
        [path] { return path.isEmpty() ? QImage() : QImage(path); },
        [this, id, animate, landed](const QImage& decoded) {
          Project* pr = findProject(id.toStdString());
          if (!pr) {   // removed while its picture decoded
            if (landed) landed(false);
            return;
          }
          {
            const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                      settings.customPageWidth,
                                                      settings.customPageHeight);
            canvas->setPageCm(page.width, page.height);
          }
          canvas->restore(pr->imagePath, pr->lines, canvas->getScale(), pr->cropRect,
                           pr->rotationQuarters, decoded, pr->mirrored);
          // Mirrors the browser storage.loadProject snap; keep-forever (expiresAt 0) is untouched.
          if (pr->meta.autoRefresh && pr->meta.expiresAt != 0) {
            pr->meta.expiresAt = core::ProjectsStore::addPeriod(nowMs(), pr->meta.refreshPeriod);
            fileStore::saveProjects(projectList);
          }
          activeProjectId = id;
          remote.session->getLink().unbind();  // a local project is not server-linked
          remoteSync->stopRemotePoll();   // no longer a server session
          docSource.currentSource = QString::fromStdString(pr->meta.source);
          docSource.currentResource = QString::fromStdString(pr->meta.resource);
          docSource.blankColor = pr->meta.blank ? QString::fromStdString(pr->meta.blankColor) : QString();
          canvas->setBlankPage(!docSource.blankColor.isEmpty());
          // Chat persistence (§12): with saving on the conversation is project-scoped; off, it survives
          // switches.
          if (settings.saveChatsWithProject) chatSession->restoreChatFromDoc(pr->chat);
          refreshActions();
          // Browser parity: storage.js restores `layout.zoom` + scroll, else fitToWindow(). Guarded so
          // the restore does not re-persist itself.
          session.setRestoring(true);
          if (pr->zoomScale > 0) {
            setZoom(pr->zoomScale);
            // Deferred a turn (browser requestAnimationFrame): the scrollbar range reflects the zoom
            // only after the layout pass.
            const int sx = pr->scrollLeft, sy = pr->scrollTop;
            QTimer::singleShot(0, this, [this, sx, sy] {
              if (scroll) {
                scroll->horizontalScrollBar()->setValue(sx);
                scroll->verticalScrollBar()->setValue(sy);
              }
              session.setRestoring(false);
            });
          } else {
            fitToWindow();   // fit the opened project to the window (matches the browser)
            session.setRestoring(false);
          }
          if (animate) playImageArrival();   // a reopened project's picture APPEARS, like any other
          notify->success(
              QString("Opened \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
          if (landed) landed(true);
        },
        [landed] { if (landed) landed(false); });
    return true;
  }

  // Shared by openServerProject and the "Open in…" hand-off so both restore the exact session.
  void MainWindow::loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                                       const QByteArray& bytes, const QString& ext) {
    // Untouched source bytes for a lossless .stencil re-bundle (empty ⇒ re-encode).
    docSource.setBytes(bytes, ext);
    parts.projects.adoptServerLayoutMeta(layout);
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings.customPageWidth,
                                              settings.customPageHeight);
    canvas->setPageCm(page.width, page.height);
    // Mirror and rotation apply before the crop (the crop lives in rotated-original space).
    int lw = 0, lh = 0;
    core::CropRect crop;
    int rot = 0;
    bool mirrored = false;
    core::Lines lines = fileStore::parseLayoutJson(layout, lw, lh, &crop, &rot, &mirrored);
    // An empty layout resets to "none" + the default tint, so a prior filter never bleeds in; it
    // lands before the picture, so the stack the load starts carries it.
    QString filter, tint;
    parseLayoutFilter(layout, settings.filterColor, filter, tint);
    applyTintColor(QColor(tint), /*asUndoStep=*/false);
    applyImageFilter(filter, false);
    canvas->loadFromImage(img, crop, rot, mirrored);
    if (!lines.empty()) canvas->setLines(lines);
  }

}  // namespace stencil::gui
