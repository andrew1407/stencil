#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "SiblingWindows.hpp"
#include "stamps.hpp"
#include "displayName.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "StencilFileSync.hpp"
#include "ProjectTitleController.hpp"
#include "IncognitoOverlay.hpp"

#include <QScrollBar>
#include <QScrollArea>
#include <QDir>
#include <QApplication>

// Creating a local project — the canvas adopted as it loads, or an incognito canvas promoted on a
// save — finding one, and whether another window has it open.

namespace stencil::gui {

  // pr.meta.name == the passed name.
  void ProjectHost::createLocalProject(const QString& name, bool announce, bool fromFile) {
    w.remote.session->getLink().unbind();  // a freshly created local project is not server-linked
    w.remoteSync->stopRemotePoll();   // no longer a server session
    Project pr;
    pr.meta.id = projectsStore.createId(nowMs(), makeSalt());
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.meta.expiresAt = core::ProjectsStore::addPeriod(
        pr.meta.updatedAt, core::ProjectsStore::DEFAULT_PERIOD);
    // A blank / remote / video-frame canvas has no path: persist the uncropped original to the
    // state dir (crop + rotation are meta) and repoint the canvas.
    QString path = w.canvas->getImagePath();
    if (path.isEmpty() && w.canvas->hasImage()) {
      const QString imgDir = fileStore::stateDir() + "/images";
      QDir().mkpath(imgDir);
      path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
      if (w.canvas->getOriginalImage().save(path, "PNG")) w.canvas->setImagePath(path);
      else path.clear();  // write failed → keep it in-memory (hasImage=false)
    }
    pr.imagePath = path;
    pr.lines = w.canvas->allLines();
    pr.cropRect = w.canvas->getCropRect();
    pr.rotationQuarters = w.canvas->getRotationQuarters();
    pr.mirrored = w.canvas->getMirrored();
    // Seed the current pan/zoom (browser #buildLayout() reads the live scale/scroll on every
    // save).
    pr.zoomScale = w.canvas->getScale();
    if (w.scroll) {
      pr.scrollLeft = w.scroll->horizontalScrollBar()->value();
      pr.scrollTop = w.scroll->verticalScrollBar()->value();
    }
    pr.meta.hasImage = !pr.imagePath.isEmpty();
    pr.meta.source = docSource.currentSource.toStdString();
    pr.meta.resource = docSource.currentResource.toStdString();
    pr.meta.blankColor = docSource.blankColor.toStdString();  // blank-fill colour (empty = ordinary image)
    pr.meta.blank = !docSource.blankColor.isEmpty();
    pr.meta.fromFile = fromFile;  // provenance: opened from a .stencil (bronze projects-list outline)
    if (w.stencilSync) pr.linkedFile = w.stencilSync->linkedPath();
    w.parts.view.stampCanvasMeta(pr.meta);     // cache image px dims + line length (cm) for the projects-list tooltip
    projectList.push_back(pr);
    activeProjectId = QString::fromStdString(pr.meta.id);
    fileStore::markProjectOpened(activeProjectId, nowMs());
    SharedState::instance().saveProjects(w.window);
    w.refreshActions();
    SiblingWindows::refreshDockMenu(projectList);  // surface the new project in the Dock "recent" list
    if (announce) w.notify->success(QString("Created \"%1\"").arg(support::shortName(name)));
  }

  void ProjectHost::adoptCanvasAsLocalProject() {
    // Incognito never persists; a server session owns its saving; an active project is
    // open/replace, not a fresh load.
    const QString serverTarget = docSource.pendingServerTarget;   // consumed either way
    docSource.pendingServerTarget.clear();
    if (incognito) return;
    if (!activeProjectId.isEmpty() || !w.remote.session->getLink().address.isEmpty()) return;
    if (!w.canvas->hasImage()) return;
    // Mirrors newProjectFromCanvas.
    QString seed = w.canvas->imageBaseName();
    if (seed.isEmpty()) {
      std::vector<core::ProjectMeta> metas;
      for (const auto& pr : projectList) metas.push_back(pr.meta);
      core::ProjectsStore tmp;
      tmp.load(metas);
      seed = QString::fromStdString(tmp.defaultName());
    }
    // The Open dialog's "Save to" pick (browser openImageModal.js `address`).
    if (!serverTarget.isEmpty()) {
      flows.createServerProject(serverTarget, seed);
      return;
    }
    createLocalProject(seed, /*announce=*/false);  // the load path already notified
    // Browser parity: storage.save() flashes "Saved".
    w.notify->success(QStringLiteral("Saved"));
  }

  // The local twin of publishIncognitoToServer; an explicit user save is not the app writing on
  // its own.
  QString ProjectHost::promoteIncognitoToLocal(const QString& name) {
    if (!w.canvas->hasImage()) return QString();
    if (incognito) {
      incognito = false;
      w.overlays.incognito->setActive(false);
      w.acts.incognito->blockSignals(true);
      w.acts.incognito->setChecked(false);
      w.acts.incognito->blockSignals(false);
      w.projectTitle->updateProjectTitle();
    }
    QString seed = name.trimmed();
    if (seed.isEmpty()) seed = w.canvas->imageBaseName();
    if (seed.isEmpty()) seed = QStringLiteral("Untitled");
    const QString unique = w.parts.chatAppliers.uniqueLocalProjectName(seed);
    createLocalProject(unique, /*announce=*/false);
    return unique;
  }

  bool ProjectHost::projectOpenInOtherWindow(const QString& id) const {
    if (id.isEmpty()) return false;
    for (QWidget* top : QApplication::topLevelWidgets()) {
      auto* mw = qobject_cast<MainWindow*>(top);
      if (mw && mw != w.window && mw->projects().activeProjectId == id) return true;
    }
    return false;
  }
}  // namespace stencil::gui
