// Closing the project this window holds while it stays in Projects — the Close Project action asks
// first, the Projects window's Close drop does not (browser ui/projects/closeProject.js) — and that
// list's drag targets on this window.
#include "MainWindow.hpp"
#include "ProjectFlows.hpp"
#include "ProjectsDialog.hpp"
#include "displayName.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "../../../support/modal/modalChrome.hpp"

namespace stencil::gui {

  // Nothing is removed, so no danger; with nothing open it only says so.
  void ProjectFlows::closeActiveProject(QWidget* over, const QRect& from, bool ask) {
    const QString id = w.activeProjectId;
    const QString serverId = id.isEmpty() ? w.remote.session->getLink().id : QString();
    if (id.isEmpty() && serverId.isEmpty()) {
      w.notify->info(QStringLiteral("No project is open"));
      return;
    }
    const Project* pr = id.isEmpty() ? nullptr : w.findProject(id.toStdString());
    QString name = pr ? QString::fromStdString(pr->meta.name) : w.remote.session->getLink().name;
    name = support::shortName(name.isEmpty() ? MainWindow::tr("Untitled") : name);
    ConfirmSpec spec;
    spec.title = MainWindow::tr("Close project?");
    spec.message = MainWindow::tr("Close \"%1\" in this window? It stays saved in Projects — nothing is removed.")
                       .arg(name);
    spec.confirmLabel = MainWindow::tr("Close");
    spec.confirmIcon = QStringLiteral("x");
    spec.flight.openRect = from;
    spec.flight.closeRect = from;
    if (ask && !confirmModal(over ? over : &w, spec)) return;
    // The question ran an event loop: another project opened meanwhile was not the one asked about.
    if (w.activeProjectId != id || (!serverId.isEmpty() && w.remote.session->getLink().id != serverId)) return;
    // The work on screen stays with the project, as when a fresh editor replaces it.
    if (!w.incognito && w.findProject(id.toStdString())) w.saveToActiveProject();
    resetToBlankEditor();
    w.notify->success(QStringLiteral("Closed \"%1\"").arg(name));
  }

  void ProjectFlows::wireProjectsDrag(ProjectsDialog& dlg, const std::function<bool()>& unsavedSession) {
    dlg.setDragZones(w.overlays.projectZones);   // the main-window drag-out zone overlay (open/new-window/remove)
    const auto holds = [this, &dlg] {
      const RemoteLink& link = w.remote.session->getLink();
      if (w.activeProjectId.isEmpty() && !link.id.isEmpty()) dlg.setOpenHere(link.id, link.address);
      else dlg.setOpenHere(w.activeProjectId);
    };
    holds();
    // Closed at once, unasked; the still-open list repaints.
    QObject::connect(&dlg, &ProjectsDialog::closeProjectRequested, &w, [this, &dlg, holds, unsavedSession] {
      closeActiveProject(&dlg, QRect(), /*ask=*/false);
      holds();
      dlg.setProjects(w.projectList, unsavedSession(), w.incognito);
    });
  }

}  // namespace stencil::gui
