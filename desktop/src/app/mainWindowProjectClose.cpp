#include "mainWindow.hpp"
#include <QScrollBar>
#include "mainWindowHelpers.hpp"
#include <QScrollArea>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "displayName.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "cropDialog.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "serverClient.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QTimer>

// Saving to the active project, clearing it and resetting to a blank editor.

namespace stencil::gui {

  void MainWindow::saveToActiveProject() {
    if (incognito_) {  // no local save while incognito…
      const QStringList servers = connections_ ? connections_->urls() : QStringList();
      if (!canvas_->hasImage()) {
        notify_->info("Nothing to save yet");
        return;
      }
      if (servers.isEmpty()) {   // no server to publish to — keep it locally instead
        notify_->success(QStringLiteral("Left incognito — saved \"%1\"")
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
    if (!remoteSession_->link().address.isEmpty()) {  // server-linked session → write back to the server
      if (!settings_.syncToServer) {
        notify_->info(
            "Sync off — not saved. Export the image/layout or use Make local copy to keep changes.");
        return;
      }
      saveToServer();
      return;
    }
    if (activeProjectId_.isEmpty()) {
      newProjectFromCanvas();
      return;
    }
    Project* pr = findProject(activeProjectId_.toStdString());
    if (!pr) {
      newProjectFromCanvas();
      return;
    }
    pr->imagePath = canvas_->imagePath();
    pr->lines = canvas_->allLines();
    pr->cropRect = canvas_->cropRect();
    pr->rotationQuarters = canvas_->rotationQuarters();
    // Every save captures the current pan/zoom (browser #buildLayout() reads the live scale/scroll
    // on every save()).
    pr->zoomScale = canvas_->scale();
    if (scroll_) {
      pr->scrollLeft = scroll_->horizontalScrollBar()->value();
      pr->scrollTop = scroll_->verticalScrollBar()->value();
    }
    pr->meta.updatedAt = nowMs();
    pr->meta.hasImage = !pr->imagePath.isEmpty();
    stampCanvasMeta(pr->meta);  // refresh cached image px dims + line length (cm) for the tooltip
    // A save must not wipe links set via the Links dialog; a fresh URL-loaded image updates them.
    if (!currentSource_.isEmpty()) pr->meta.source = currentSource_.toStdString();
    if (!currentResource_.isEmpty()) pr->meta.resource = currentResource_.toStdString();
    // Chat persistence (§12): with the opt-in off, an earlier saved chat is left alone.
    if (settings_.saveChatsWithProject) pr->chat = buildActiveChatDoc();
    fileStore::saveProjects(projectList_);
    refreshDockMenu();  // bump it to the top of the Dock "recent" list
    notify_->success(
        QString("Saved to \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
  }

  // Mirrors the browser #clear-storage handler; hidden for server-linked sessions
  // (refreshActions).
  void MainWindow::clearCurrentProject() {
    const bool hasProject = !activeProjectId_.isEmpty();
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
      const std::string id = activeProjectId_.toStdString();
      projectList_.erase(
          std::remove_if(projectList_.begin(), projectList_.end(),
                         [&](const Project& p) { return p.meta.id == id; }),
          projectList_.end());
      fileStore::saveProjects(projectList_);
    }
    resetToBlankEditor();
    refreshDockMenu();  // drop the cleared project from the Dock "recent" list
    // A success, not a notice (browser controlsBinder.js shows the same strings in --success).
    notify_->success(hasProject ? "Project cleared" : "Editor cleared");
  }

  // The browser's storage.newTemporary(); link().unbind() is defensive, the trash button is hidden
  // for server sessions.
  void MainWindow::resetToBlankEditor() {
    activeProjectId_.clear();
    remoteSession_->link().unbind();
    currentSource_.clear();
    currentResource_.clear();
    blankColor_.clear();
    // Snapshot before clearImage repaints (browser ghostOut); hosted on the viewport, or the
    // overlay spills across the docks.
    const bool reduced = support::motionReduced();
    if (!reduced && canvas_ && scroll_ && scroll_->viewport()) {
      const QRect vis = canvas_->visibleRegion().boundingRect();
      if (!vis.isEmpty())
        DisintegrateOverlay::overRect(canvas_, vis, scroll_->viewport(),
                                      DisintegrateOverlay::Sweep::Fall);
    }
    canvas_->clearImage();
    updateStatusIdle();   // the last hovered pixel must not outlive the image it named
    // Keep the empty-canvas invitation hidden until the dust lands, or the clear reads as
    // happening twice (browser .canvas-clearing).
    if (!reduced) {
      canvas_->setIdleHintHidden(true);
      QTimer::singleShot(DisintegrateOverlay::DUST_MS, canvas_,
                         [this] { if (canvas_) canvas_->setIdleHintHidden(false); });
    }
    refreshActions();
    saveSessionNow();  // persist the cleared state so it doesn't restore on next launch
  }

}  // namespace stencil::gui
