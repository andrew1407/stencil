#include "MainWindow.hpp"
#include <QScrollBar>
#include <QScrollArea>
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "displayName.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "CropDialog.hpp"
#include "numericInput.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QDir>

// Creating, finding and erasing local projects.

namespace stencil::gui {

  void MainWindow::newProjectFromCanvas() {
    if (incognito) {  // an explicit promotion: leave incognito and keep the work
      const QString promoted = promoteIncognitoToLocal();
      if (!promoted.isEmpty()) {
        notify->success(QStringLiteral("Left incognito — saved \"%1\"")
                             .arg(support::shortName(promoted)));
        return;
      }
      notify->info("Nothing to save yet");
      return;
    }
    // Named after the image, as in the browser, else a unique "Untitled N".
    QString seed = canvas->hasImage() ? canvas->imageBaseName() : QString();
    if (seed.isEmpty()) {
      std::vector<core::ProjectMeta> metas;
      for (const auto& pr : projectList) metas.push_back(pr.meta);
      core::ProjectsStore tmp;
      tmp.load(metas);
      seed = QString::fromStdString(tmp.defaultName());
    }
    // Save goes dead with the reason until the name is saveable (the projects list's rules).
    PromptSpec spec;
    spec.title = tr("New Project");
    spec.titleIcon = QStringLiteral("plus-circle");
    spec.message = tr("Project name:");
    spec.defaultValue = seed;
    spec.validate = [this](const QString& name) {
      const auto check = checkProjectName(name, QString());
      return check.ok ? QString() : QString::fromStdString(check.reason);
    };
    const auto name = promptModal(this, spec);
    if (!name || name->isEmpty()) return;
    createProject(*name);
  }

  Project* MainWindow::findProject(const std::string& id) {
    auto it = std::find_if(projectList.begin(), projectList.end(),
                           [&](const Project& p) { return p.meta.id == id; });
    return it == projectList.end() ? nullptr : &*it;
  }

  // Resetting the editor when it is the open one (browser removeProject → storage.newTemporary).
  // The caller persists + refreshes.
  void MainWindow::eraseLocalProject(const QString& id) {
    const std::string sid = id.toStdString();
    projectList.erase(
        std::remove_if(projectList.begin(), projectList.end(),
                       [&](const Project& p) { return p.meta.id == sid; }),
        projectList.end());
    if (activeProjectId == id) resetToBlankEditor();
  }

  void MainWindow::persistSettings() {
    if (!incognito) fileStore::saveSettings(settings);
  }

  // With ≥1 server connected, ask where to save (browser local-vs-server target choice); the
  // incognito guard lives at each call site.
  void MainWindow::createProject(const QString& name) {
    const QStringList servers = connections ? connections->urls() : QStringList();
    if (servers.isEmpty()) {
      createLocalProject(name);
      return;
    }
    // The browser's save-target select (base.js fillTargetSelect).
    ChooseSpec spec;
    spec.title = tr("Save project");
    spec.message = tr("Where should it be saved?");
    spec.confirmLabel = tr("Save");
    spec.confirmIcon = QStringLiteral("save");
    spec.options.push_back({QString(), tr("Local (this computer)")});
    for (const QString& s : servers) spec.options.push_back({s, s});
    const auto choice = chooseModal(this, spec);
    if (!choice) return;
    if (choice->isEmpty()) {
      createLocalProject(name);
    } else {
      createServerProject(*choice, name);
    }
  }

  // pr.meta.name == the passed name.
  void MainWindow::createLocalProject(const QString& name, bool announce, bool fromFile) {
    remoteSession->getLink().unbind();  // a freshly created local project is not server-linked
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
    // Seed the current pan/zoom (browser #buildLayout() reads the live scale/scroll on every
    // save).
    pr.zoomScale = canvas->getScale();
    if (scroll) {
      pr.scrollLeft = scroll->horizontalScrollBar()->value();
      pr.scrollTop = scroll->verticalScrollBar()->value();
    }
    pr.meta.hasImage = !pr.imagePath.isEmpty();
    pr.meta.source = currentSource.toStdString();
    pr.meta.resource = currentResource.toStdString();
    pr.meta.blankColor = blankColor.toStdString();  // blank-fill colour (empty = ordinary image)
    pr.meta.blank = !blankColor.isEmpty();
    pr.meta.fromFile = fromFile;  // provenance: opened from a .stencil (bronze projects-list outline)
    stampCanvasMeta(pr.meta);     // cache image px dims + line length (cm) for the projects-list tooltip
    projectList.push_back(pr);
    activeProjectId = QString::fromStdString(pr.meta.id);
    fileStore::saveProjects(projectList);
    refreshActions();
    refreshDockMenu();  // surface the new project in the Dock "recent" list
    if (announce) notify->success(QString("Created \"%1\"").arg(support::shortName(name)));
  }

}  // namespace stencil::gui
