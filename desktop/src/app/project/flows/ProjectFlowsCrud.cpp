#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "ProjectFlows.hpp"
#include <QScrollArea>
#include "displayName.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "../../../support/modal/imageAnchor.hpp"
#include "SiblingWindows.hpp"

// Saving to the active project, clearing it and resetting to a blank editor.

namespace stencil::gui {

  // Mirrors the browser #clear-storage handler; hidden for server-linked sessions
  // (refreshActions).
  void ProjectFlows::clearCurrentProject() {
    const bool hasProject = !w.activeProjectId.isEmpty();
    const QString title = hasProject ? MainWindow::tr("Clear project") : MainWindow::tr("Clear editor");
    const QString msg = hasProject
        ? MainWindow::tr("Clear this project (image + lines) from storage?")
        : MainWindow::tr("Clear this editor (image + lines)?");
    ConfirmSpec spec;
    spec.title = title;
    spec.message = msg;
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;   // browser parity: the Clear-project confirm is red
    // Clearing shrinks the Image cluster, which slides this trash along the row: the answer pours
    // into where the icon LANDS, not where it was pressed (browser: emptiedControlRect).
    QPointer<MainWindow> self(&w);
    QPointer<QWidget> trash(w.buttonForAction(w.acts.clearProject));
    spec.flight.closeRectFor = [self, trash](bool yes) {
      return yes && self && trash ? emptiedControlRect(self.data(), trash.data()) : QRect();
    };
    if (!confirmModal(&w, spec)) return;
    if (hasProject) {
      const std::string id = w.activeProjectId.toStdString();
      w.projectList.erase(
          std::remove_if(w.projectList.begin(), w.projectList.end(),
                         [&](const Project& p) { return p.meta.id == id; }),
          w.projectList.end());
      SharedState::instance().saveProjects(&w);
    }
    resetToBlankEditor();
    SiblingWindows::refreshDockMenu(w.projectList);  // drop the cleared project from the Dock "recent" list
    // A success, not a notice (browser controlsBinder.js shows the same strings in --success).
    w.notify->success(hasProject ? "Project cleared" : "Editor cleared");
  }

  // The browser's storage.newTemporary(); link().unbind() is defensive, the trash button is hidden
  // for server sessions.
  void ProjectFlows::resetToBlankEditor() {
    w.activeProjectId.clear();
    w.remote.session->getLink().unbind();
    w.docSource.currentSource.clear();
    w.docSource.currentResource.clear();
    w.docSource.blankColor.clear();
    // Snapshot before clearImage repaints (browser ghostOut); hosted on the viewport, or the
    // overlay spills across the docks.
    const bool reduced = support::motionReduced();
    if (!reduced && w.canvas && w.scroll && w.scroll->viewport()) {
      const QRect vis = w.canvas->visibleRegion().boundingRect();
      if (!vis.isEmpty())
        DisintegrateOverlay::overRect(w.canvas, vis, w.scroll->viewport(),
                                      DisintegrateOverlay::Sweep::FALL, false,
                                      DisintegrateOverlay::DUST_MAX_CELLS, CANVAS_DUST_MS);
    }
    w.canvas->clearImage();
    w.updateStatusIdle();   // the last hovered pixel must not outlive the image it named
    // Keep the empty-canvas invitation hidden until the dust lands, or the clear reads as
    // happening twice (browser .canvas-clearing).
    if (!reduced) {
      w.canvas->setIdleHintHidden(true);
      QTimer::singleShot(CANVAS_DUST_MS, w.canvas,
                         [this] { if (w.canvas) w.canvas->setIdleHintHidden(false); });
    }
    w.refreshActions();
    w.saveSessionNow();  // persist the cleared state so it doesn't restore on next launch
  }

  void ProjectFlows::newProjectFromCanvas() {
    if (w.incognito) {  // an explicit promotion: leave incognito and keep the work
      const QString promoted = w.promoteIncognitoToLocal();
      if (!promoted.isEmpty()) {
        w.notify->success(QStringLiteral("Left incognito — saved \"%1\"")
                             .arg(support::shortName(promoted)));
        return;
      }
      w.notify->info("Nothing to save yet");
      return;
    }
    // Named after the image, as in the browser, else a unique "Untitled N".
    QString seed = w.canvas->hasImage() ? w.canvas->imageBaseName() : QString();
    if (seed.isEmpty()) {
      std::vector<core::ProjectMeta> metas;
      for (const auto& pr : w.projectList) metas.push_back(pr.meta);
      core::ProjectsStore tmp;
      tmp.load(metas);
      seed = QString::fromStdString(tmp.defaultName());
    }
    // Save goes dead with the reason until the name is saveable (the projects list's rules).
    PromptSpec spec;
    spec.title = MainWindow::tr("New Project");
    spec.titleIcon = QStringLiteral("plus-circle");
    spec.message = MainWindow::tr("Project name:");
    spec.defaultValue = seed;
    spec.validate = [this](const QString& name) {
      const auto check = w.checkProjectName(name, QString());
      return check.ok ? QString() : QString::fromStdString(check.reason);
    };
    const auto name = promptModal(&w, spec);
    if (!name || name->isEmpty()) return;
    createProject(*name);
  }

  // Resetting the editor when it is the open one (browser removeProject → storage.newTemporary).
  // The caller persists + refreshes.
  void ProjectFlows::eraseLocalProject(const QString& id) {
    const std::string sid = id.toStdString();
    w.projectList.erase(
        std::remove_if(w.projectList.begin(), w.projectList.end(),
                       [&](const Project& p) { return p.meta.id == sid; }),
        w.projectList.end());
    if (w.activeProjectId == id) resetToBlankEditor();
  }

  // With ≥1 server connected, ask where to save (browser local-vs-server target choice); the
  // incognito guard lives at each call site.
  void ProjectFlows::createProject(const QString& name) {
    const QStringList servers = w.remote.connections ? w.remote.connections->urls() : QStringList();
    if (servers.isEmpty()) {
      w.createLocalProject(name);
      return;
    }
    // The browser's save-target select (base.js fillTargetSelect).
    ChooseSpec spec;
    spec.title = MainWindow::tr("Save project");
    spec.message = MainWindow::tr("Where should it be saved?");
    spec.confirmLabel = MainWindow::tr("Save");
    spec.confirmIcon = QStringLiteral("save");
    spec.options.push_back({QString(), MainWindow::tr("Local (this computer)")});
    for (const QString& s : servers) spec.options.push_back({s, s});
    const auto choice = chooseModal(&w, spec);
    if (!choice) return;
    if (choice->isEmpty()) {
      w.createLocalProject(name);
    } else {
      createServerProject(*choice, name);
    }
  }

  void ProjectFlows::openProjectInNewWindow(const QString& id) {
    // A fresh window loads the saved projects itself; it owns itself and dies on close.
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    if (!win->loadProjectIntoCanvas(id)) {
      w.notify->error("Could not open the project in a new window");
      win->close();  // auto-close the failed load
    }
  }
}  // namespace stencil::gui
