#include "MainWindow.hpp"
#include "ChatSessionController.hpp"
#include <QScrollBar>
#include "mainWindowHelpers.hpp"
#include <QScrollArea>
#include "displayName.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "SiblingWindows.hpp"

// Saving to the active project, clearing it and resetting to a blank editor.

namespace stencil::gui {

  void MainWindow::saveToActiveProject() {
    if (incognito) {  // no local save while incognito…
      const QStringList servers = remote.connections ? remote.connections->urls() : QStringList();
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
      parts.projects.publishIncognitoToServer(target);
      return;
    }
    if (!remote.session->getLink().address.isEmpty()) {  // server-linked session → write back to the server
      if (!settings.syncToServer) {
        notify->info(
            "Sync off — not saved. Export the image/layout or use Make local copy to keep changes.");
        return;
      }
      parts.projects.saveToServer();
      return;
    }
    if (activeProjectId.isEmpty()) {
      parts.projects.newProjectFromCanvas();
      return;
    }
    Project* pr = findProject(activeProjectId.toStdString());
    if (!pr) {
      parts.projects.newProjectFromCanvas();
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
    parts.view.stampCanvasMeta(pr->meta);  // refresh cached image px dims + line length (cm) for the tooltip
    // A save must not wipe links set via the Links dialog; a fresh URL-loaded image updates them.
    if (!docSource.currentSource.isEmpty()) pr->meta.source = docSource.currentSource.toStdString();
    if (!docSource.currentResource.isEmpty()) pr->meta.resource = docSource.currentResource.toStdString();
    // Chat persistence (§12): with the opt-in off, an earlier saved chat is left alone.
    if (settings.saveChatsWithProject) pr->chat = chatSession->buildActiveChatDoc();
    fileStore::saveProjects(projectList);
    SiblingWindows::refreshDockMenu(projectList);  // bump it to the top of the Dock "recent" list
    notify->success(
        QString("Saved to \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
  }



}  // namespace stencil::gui
