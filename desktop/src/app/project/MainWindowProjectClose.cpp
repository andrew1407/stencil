#include "MainWindow.hpp"
#include <QScrollBar>
#include "mainWindowHelpers.hpp"
#include <QScrollArea>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "displayName.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "CropDialog.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "ServerClient.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QTimer>

// Saving to the active project, clearing it and resetting to a blank editor.

namespace stencil::gui {

  void MainWindow::saveToActiveProject() {
    if (incognito) {  // no local save while incognito…
      const QStringList servers = connections ? connections->urls() : QStringList();
      if (!canvas->hasImage()) {
        notify->info("Nothing to save yet");
        return;
      }
      if (servers.isEmpty()) {   // no server to publish to — keep it locally instead
        notify->success(QStringLiteral("Left incognito — saved \"%1\"")
                             .arg(support::shortName(promoteIncognitoToLocal())));
        return;
      }
      // Publishing to a server leaves incognito (browser "Save to server" parity).
      QString target = servers.first();
      if (servers.size() > 1) {
        ChooseSpec spec;   // browser projectsModal.js saveToServer
        spec.title = tr("Save to server");
        spec.message = tr("Save this incognito project to which server?");
        spec.confirmLabel = tr("Save");
        spec.confirmIcon = QStringLiteral("upload");
        for (const QString& s : servers) spec.options.push_back({s, s});
        const auto choice = chooseModal(this, spec);
        if (!choice) return;
        target = *choice;
      }
      publishIncognitoToServer(target);
      return;
    }
    if (!remoteSession->getLink().address.isEmpty()) {  // server-linked session → write back to the server
      if (!settings.syncToServer) {
        notify->info(
            "Sync off — not saved. Export the image/layout or use Make local copy to keep changes.");
        return;
      }
      saveToServer();
      return;
    }
    if (activeProjectId.isEmpty()) {
      newProjectFromCanvas();
      return;
    }
    Project* pr = findProject(activeProjectId.toStdString());
    if (!pr) {
      newProjectFromCanvas();
      return;
    }
    pr->imagePath = canvas->getImagePath();
    pr->lines = canvas->allLines();
    pr->cropRect = canvas->getCropRect();
    pr->rotationQuarters = canvas->getRotationQuarters();
    // Every save captures the current pan/zoom (browser #buildLayout() reads the live scale/scroll
    // on every save()).
    pr->zoomScale = canvas->getScale();
    if (scroll) {
      pr->scrollLeft = scroll->horizontalScrollBar()->value();
      pr->scrollTop = scroll->verticalScrollBar()->value();
    }
    pr->meta.updatedAt = nowMs();
    pr->meta.hasImage = !pr->imagePath.isEmpty();
    stampCanvasMeta(pr->meta);  // refresh cached image px dims + line length (cm) for the tooltip
    // A save must not wipe links set via the Links dialog; a fresh URL-loaded image updates them.
    if (!currentSource.isEmpty()) pr->meta.source = currentSource.toStdString();
    if (!currentResource.isEmpty()) pr->meta.resource = currentResource.toStdString();
    // Chat persistence (§12): with the opt-in off, an earlier saved chat is left alone.
    if (settings.saveChatsWithProject) pr->chat = buildActiveChatDoc();
    fileStore::saveProjects(projectList);
    refreshDockMenu();  // bump it to the top of the Dock "recent" list
    notify->success(
        QString("Saved to \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
  }

  // Mirrors the browser #clear-storage handler; hidden for server-linked sessions
  // (refreshActions).
  void MainWindow::clearCurrentProject() {
    const bool hasProject = !activeProjectId.isEmpty();
    const QString title = hasProject ? tr("Clear project") : tr("Clear editor");
    const QString msg = hasProject
        ? tr("Clear this project (image + lines) from storage?")
        : tr("Clear this editor (image + lines)?");
    ConfirmSpec spec;
    spec.title = title;
    spec.message = msg;
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;   // browser parity: the Clear-project confirm is red
    if (!confirmModal(this, spec)) return;
    if (hasProject) {
      const std::string id = activeProjectId.toStdString();
      projectList.erase(
          std::remove_if(projectList.begin(), projectList.end(),
                         [&](const Project& p) { return p.meta.id == id; }),
          projectList.end());
      fileStore::saveProjects(projectList);
    }
    resetToBlankEditor();
    refreshDockMenu();  // drop the cleared project from the Dock "recent" list
    // A success, not a notice (browser controlsBinder.js shows the same strings in --success).
    notify->success(hasProject ? "Project cleared" : "Editor cleared");
  }

  // The browser's storage.newTemporary(); link().unbind() is defensive, the trash button is hidden
  // for server sessions.
  void MainWindow::resetToBlankEditor() {
    activeProjectId.clear();
    remoteSession->getLink().unbind();
    currentSource.clear();
    currentResource.clear();
    blankColor.clear();
    // Snapshot before clearImage repaints (browser ghostOut); hosted on the viewport, or the
    // overlay spills across the docks.
    const bool reduced = support::motionReduced();
    if (!reduced && canvas && scroll && scroll->viewport()) {
      const QRect vis = canvas->visibleRegion().boundingRect();
      if (!vis.isEmpty())
        DisintegrateOverlay::overRect(canvas, vis, scroll->viewport(),
                                      DisintegrateOverlay::Sweep::FALL, false,
                                      DisintegrateOverlay::DUST_MAX_CELLS, CANVAS_DUST_MS);
    }
    canvas->clearImage();
    updateStatusIdle();   // the last hovered pixel must not outlive the image it named
    // Keep the empty-canvas invitation hidden until the dust lands, or the clear reads as
    // happening twice (browser .canvas-clearing).
    if (!reduced) {
      canvas->setIdleHintHidden(true);
      QTimer::singleShot(CANVAS_DUST_MS, canvas,
                         [this] { if (canvas) canvas->setIdleHintHidden(false); });
    }
    refreshActions();
    saveSessionNow();  // persist the cleared state so it doesn't restore on next launch
  }

}  // namespace stencil::gui
