#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "ChatSessionController.hpp"
#include <QScrollBar>
#include "stamps.hpp"
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

  void ProjectHost::saveToActiveProject() {
    if (incognito) {  // no local save while incognito…
      const QStringList servers = w.remote.conns() ? w.remote.conns()->urls() : QStringList();
      if (!w.canvas->hasImage()) {
        w.notify->info("Nothing to save yet");
        return;
      }
      if (servers.isEmpty()) {   // no server to publish to — keep it locally instead
        w.notify->success(QStringLiteral("Left incognito — saved \"%1\"")
                             .arg(support::shortName(promoteIncognitoToLocal())));
        return;
      }
      // Publishing to a server leaves incognito (browser "Save to server" parity).
      QString target = servers.first();
      if (servers.size() > 1) {
        ChooseSpec spec;   // browser projectsModal.js saveToServer
        spec.title = MainWindow::tr("Save to server");
        spec.message = MainWindow::tr("Save this incognito project to which server?");
        spec.confirmLabel = MainWindow::tr("Save");
        spec.confirmIcon = QStringLiteral("upload");
        for (const QString& s : servers) spec.options.push_back({s, s});
        const auto choice = chooseModal(w.window, spec);
        if (!choice) return;
        target = *choice;
      }
      flows.publishIncognitoToServer(target);
      return;
    }
    if (!w.remote.session->getLink().address.isEmpty()) {  // server-linked session → write back to the server
      if (!w.settings.syncToServer) {
        w.notify->info(
            "Sync off — not saved. Export the image/layout or use Make local copy to keep changes.");
        return;
      }
      flows.saveToServer();
      return;
    }
    if (activeProjectId.isEmpty()) {
      flows.newProjectFromCanvas();
      return;
    }
    Project* pr = findProject(activeProjectId.toStdString());
    if (!pr) {
      flows.newProjectFromCanvas();
      return;
    }
    pr->imagePath = w.canvas->getImagePath();
    pr->lines = w.canvas->allLines();
    pr->cropRect = w.canvas->getCropRect();
    pr->rotationQuarters = w.canvas->getRotationQuarters();
    pr->mirrored = w.canvas->getMirrored();
    // Every save captures the current pan/zoom (browser #buildLayout() reads the live scale/scroll
    // on every save()).
    pr->zoomScale = w.canvas->getScale();
    if (w.scroll) {
      pr->scrollLeft = w.scroll->horizontalScrollBar()->value();
      pr->scrollTop = w.scroll->verticalScrollBar()->value();
    }
    pr->meta.updatedAt = nowMs();
    pr->meta.hasImage = !pr->imagePath.isEmpty();
    w.parts.view.stampCanvasMeta(pr->meta);  // refresh cached image px dims + line length (cm) for the tooltip
    // A save must not wipe links set via the Links dialog; a fresh URL-loaded image updates them.
    if (!docSource.currentSource.isEmpty()) pr->meta.source = docSource.currentSource.toStdString();
    if (!docSource.currentResource.isEmpty()) pr->meta.resource = docSource.currentResource.toStdString();
    // Chat persistence (§12): with the opt-in off, an earlier saved chat is left alone.
    if (w.settings.saveChatsWithProject) pr->chat = w.chatSession->buildActiveChatDoc();
    SharedState::instance().saveProjects(w.window);
    SiblingWindows::refreshDockMenu(projectList);  // bump it to the top of the Dock "recent" list
    w.notify->success(
        QString("Saved to \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
  }



}  // namespace stencil::gui
