#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "SiblingWindows.hpp"
#include "mainWindowHelpers.hpp"
#include "displayName.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTitleController.hpp"
#include "IncognitoOverlay.hpp"

#include <QScrollBar>
#include <QScrollArea>
#include <QDir>
#include <QApplication>

// Creating a local project — the canvas adopted as it loads, or an incognito canvas promoted on a
// save — finding one, and whether another window has it open.

namespace stencil::gui {

  Project* MainWindow::findProject(const std::string& id) {
    auto it = std::find_if(projectList.begin(), projectList.end(),
                           [&](const Project& p) { return p.meta.id == id; });
    return it == projectList.end() ? nullptr : &*it;
  }

  void MainWindow::persistSettings() {
    if (!incognito) SharedState::instance().saveSettings(settings, this);
  }

  // pr.meta.name == the passed name.
  void MainWindow::createLocalProject(const QString& name, bool announce, bool fromFile) {
    remote.session->getLink().unbind();  // a freshly created local project is not server-linked
    remoteSync->stopRemotePoll();   // no longer a server session
    Project pr;
    pr.meta.id = projectsStore.createId(nowMs(), makeSalt());
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.meta.expiresAt = core::ProjectsStore::addPeriod(
        pr.meta.updatedAt, core::ProjectsStore::DEFAULT_PERIOD);
    // A blank / remote / video-frame canvas has no path: persist the uncropped original to the
    // state dir (crop + rotation are meta) and repoint the canvas.
    QString path = canvas->getImagePath();
    if (path.isEmpty() && canvas->hasImage()) {
      const QString imgDir = fileStore::stateDir() + "/images";
      QDir().mkpath(imgDir);
      path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
      if (canvas->getOriginalImage().save(path, "PNG")) canvas->setImagePath(path);
      else path.clear();  // write failed → keep it in-memory (hasImage=false)
    }
    pr.imagePath = path;
    pr.lines = canvas->allLines();
    pr.cropRect = canvas->getCropRect();
    pr.rotationQuarters = canvas->getRotationQuarters();
    pr.mirrored = canvas->getMirrored();
    // Seed the current pan/zoom (browser #buildLayout() reads the live scale/scroll on every
    // save).
    pr.zoomScale = canvas->getScale();
    if (scroll) {
      pr.scrollLeft = scroll->horizontalScrollBar()->value();
      pr.scrollTop = scroll->verticalScrollBar()->value();
    }
    pr.meta.hasImage = !pr.imagePath.isEmpty();
    pr.meta.source = docSource.currentSource.toStdString();
    pr.meta.resource = docSource.currentResource.toStdString();
    pr.meta.blankColor = docSource.blankColor.toStdString();  // blank-fill colour (empty = ordinary image)
    pr.meta.blank = !docSource.blankColor.isEmpty();
    pr.meta.fromFile = fromFile;  // provenance: opened from a .stencil (bronze projects-list outline)
    parts.view.stampCanvasMeta(pr.meta);     // cache image px dims + line length (cm) for the projects-list tooltip
    projectList.push_back(pr);
    activeProjectId = QString::fromStdString(pr.meta.id);
    SharedState::instance().saveProjects(this);
    refreshActions();
    SiblingWindows::refreshDockMenu(projectList);  // surface the new project in the Dock "recent" list
    if (announce) notify->success(QString("Created \"%1\"").arg(support::shortName(name)));
  }

  void MainWindow::adoptCanvasAsLocalProject() {
    // Incognito never persists; a server session owns its saving; an active project is
    // open/replace, not a fresh load.
    const QString serverTarget = docSource.pendingServerTarget;   // consumed either way
    docSource.pendingServerTarget.clear();
    if (incognito) return;
    if (!activeProjectId.isEmpty() || !remote.session->getLink().address.isEmpty()) return;
    if (!canvas->hasImage()) return;
    // Mirrors newProjectFromCanvas.
    QString seed = canvas->imageBaseName();
    if (seed.isEmpty()) {
      std::vector<core::ProjectMeta> metas;
      for (const auto& pr : projectList) metas.push_back(pr.meta);
      core::ProjectsStore tmp;
      tmp.load(metas);
      seed = QString::fromStdString(tmp.defaultName());
    }
    // The Open dialog's "Save to" pick (browser openImageModal.js `address`).
    if (!serverTarget.isEmpty()) {
      parts.projects.createServerProject(serverTarget, seed);
      return;
    }
    createLocalProject(seed, /*announce=*/false);  // the load path already notified
    // Browser parity: storage.save() flashes "Saved".
    notify->success(QStringLiteral("Saved"));
  }

  // The local twin of publishIncognitoToServer; an explicit user save is not the app writing on
  // its own.
  QString MainWindow::promoteIncognitoToLocal(const QString& name) {
    if (!canvas->hasImage()) return QString();
    if (incognito) {
      incognito = false;
      overlays.incognito->setActive(false);
      acts.incognito->blockSignals(true);
      acts.incognito->setChecked(false);
      acts.incognito->blockSignals(false);
      projectTitle->updateProjectTitle();
    }
    QString seed = name.trimmed();
    if (seed.isEmpty()) seed = canvas->imageBaseName();
    if (seed.isEmpty()) seed = QStringLiteral("Untitled");
    const QString unique = parts.chatAppliers.uniqueLocalProjectName(seed);
    createLocalProject(unique, /*announce=*/false);
    return unique;
  }

  bool MainWindow::projectOpenInOtherWindow(const QString& id) const {
    if (id.isEmpty()) return false;
    for (QWidget* w : QApplication::topLevelWidgets()) {
      auto* mw = qobject_cast<MainWindow*>(w);
      if (mw && mw != this && mw->activeProjectId == id) return true;
    }
    return false;
  }

}  // namespace stencil::gui
