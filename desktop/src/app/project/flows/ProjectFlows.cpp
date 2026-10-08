// The projects dialog: opening it, and every action it can come back with — open, remove, rename,
// recolour, expiry and the batch transfers between local storage and a server.
#include "projectThumbs.hpp"
#include "SiblingWindows.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ProjectTitleController.hpp"
#include "displayName.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSession.hpp"
#include "ProjectTransferController.hpp"
#include "theme.hpp"

namespace stencil::gui {

  namespace {
    // How long a rebuild waits for the removal wipe (ProjectsDialog::retireRow's own gate).
    int rebuildAfterWipeMs() { return support::isDustAllowed() ? DisintegrateOverlay::DUST_MS : 0; }
  }  // namespace

  void ProjectFlows::openProjects() {
    // Expiry sweep (one week).
    w.projectsStore.clearAll();
    std::vector<core::ProjectMeta> metas;
    for (const auto& pr : w.projectList) metas.push_back(pr.meta);
    w.projectsStore.load(metas);
    const auto expired = w.projectsStore.sweepExpired(nowMs());
    if (!expired.empty()) {
      w.projectList.erase(
          std::remove_if(w.projectList.begin(), w.projectList.end(),
                         [&](const Project& p) {
                           return std::find(expired.begin(), expired.end(),
                                            p.meta.id) != expired.end();
                         }),
          w.projectList.end());
      // Not gated by incognito: other saved projects, not the incognito editor's content.
      fileStore::saveProjects(w.projectList);
    }

    // Cached thumbnails show at once; the rest arrive row by row, the open project's among them.
    projectThumbs::Look look{resolveDark(w.settings.themeMode), w.settings.accentColor,
                             w.settings.showPoints, w.settings.showLines, w.settings.imageFilter,
                             w.tools.filterColorValue};
    const core::PageSize page = naturalPageCm(w.pageSizeValue(), w.settings.customPageWidth,
                                              w.settings.customPageHeight);
    look.pageWidthCm = page.width;
    look.pageHeightCm = page.height;
    QHash<QString, QPixmap> thumbs;
    std::vector<projectThumbs::Source> missing;
    for (const auto& pr : w.projectList) {
      const QString id = QString::fromStdString(pr.meta.id);
      if (id == w.activeProjectId && w.canvas->hasImage()) {
        missing.push_back({id, w.canvas->getImagePath(), w.canvas->getOriginalImage(), w.canvas->allLines(),
                           w.canvas->getCropRect(), w.canvas->getRotationQuarters(), QString(),
                           w.canvas->getMirrored()});
        continue;
      }
      if (pr.imagePath.isEmpty()) continue;
      const QString key = projectThumbs::keyOf(pr, look);
      const QPixmap hit = projectThumbs::cached(key);
      if (!hit.isNull()) thumbs.insert(id, hit);
      else missing.push_back({id, pr.imagePath, QImage(), pr.lines, pr.cropRect, pr.rotationQuarters, key, pr.mirrored});
    }
    ProjectsDialog dlg(w.projectList, nowMs(), w.remote.connections, thumbs,
                       &w, w.activeProjectId, accentPrimary(w.settings.accentColor));
    projectThumbs::build(&dlg, std::move(missing), look,
                         [&dlg](const QString& id, const QPixmap& thumb) { dlg.setLocalThumb(id, thumb); });
    // No project open: the list pins this window as "Temporary (unsaved)". Re-asked per removal — deleting the OPEN project
    // resets this window to a blank editor — and it travels WITH the list so the batch bar and row repaint together.
    const auto unsavedSession = [this] {
      return w.activeProjectId.isEmpty() && w.remote.session->getLink().id.isEmpty();
    };
    dlg.setTemporary(unsavedSession(), w.incognito);
    dlg.setDragZones(w.overlays.projectZones);   // the main-window drag-out zone overlay (open/new-window/remove)
    // Handled WHILE the dialog is up: it confirms itself, we remove, it repaints.
    QObject::connect(&dlg, &ProjectsDialog::clearAllRequested, &w, [this, &dlg, unsavedSession] {
      const int n = static_cast<int>(w.projectList.size());
      const bool hadActive = !w.activeProjectId.isEmpty();
      w.projectList.clear();
      if (hadActive) resetToBlankEditor();   // the open one went with them
      fileStore::saveProjects(w.projectList);
      w.refreshActions();
      SiblingWindows::refreshDockMenu(w.projectList);
      // Rows are still scattering; rebuild once the motes have landed (browser: beginRemoval).
      QPointer<ProjectsDialog> live(&dlg);
      QTimer::singleShot(rebuildAfterWipeMs(), &w, [this, live, unsavedSession] {
        if (live) live->setProjects(w.projectList, unsavedSession(), w.incognito);
      });
      if (n) w.notify->success(clearedToast(n));
    });
    // Same stay-open pattern: remove, then repaint once the dust lands.
    QObject::connect(&dlg, &ProjectsDialog::removeRequested, &w,
                     [this, &dlg, unsavedSession](const QVector<QPair<QString, QString>>& items) {
      QPointer<ProjectsDialog> live(&dlg);
      const bool single = items.size() == 1 && items.first().second.isEmpty();
      // A project open in another window cannot be removed (browser "open in another tab" guard).
      if (single && w.projectOpenInOtherWindow(items.first().first)) {
        w.notify->error("That project is open in another window — close it there first");
        if (live) live->setProjects(w.projectList);
        return;
      }
      for (const auto& pr : items) {
        if (pr.second.isEmpty()) {
          eraseLocalProject(pr.first);
        } else if (auto* c = w.remote.connections ? w.remote.connections->find(pr.second) : nullptr) {
          c->deleteProjectAsync(pr.first, [](bool) {});  // fire-and-forget; list refresh is independent
        }
      }
      fileStore::saveProjects(w.projectList);
      w.refreshActions();
      SiblingWindows::refreshDockMenu(w.projectList);  // drop it from the Dock "recent" list
      w.notify->success(clearedToast(static_cast<int>(items.size())));
      QTimer::singleShot(rebuildAfterWipeMs(), &w, [this, live, unsavedSession] {
        if (live) live->setProjects(w.projectList, unsavedSession(), w.incognito);
      });
    });
    // Inline rename: same stay-open pattern.
    QObject::connect(&dlg, &ProjectsDialog::renameRequested, &w,
                     [this, &dlg](const QString& id, const QString& name) {
      renameProjectById(id, name);
      dlg.setProjects(w.projectList);
    });
    // "Set expiration" / "Open in another app": the list stays up.
    const QString botUser = w.settings.telegramBotUsername.trimmed();
    const bool browserTarget = !w.settings.browserBaseUrl.trimmed().isEmpty();
    dlg.setOpenInAvailable(browserTarget, browserTarget || !botUser.isEmpty());
    QObject::connect(&dlg, &ProjectsDialog::openInRequested, &w, [this](const QString& id, const QString& serverUrl,
                     const QRect& closeRect) { openInAnotherAppFor(id, serverUrl, closeRect); });
    w.parts.projectCopy.wireProjectsList(dlg);   // "Make a copy" (app/project/copy/ProjectCopyFlow.cpp)
    QObject::connect(&dlg, &ProjectsDialog::expirationRequested, &w,
                     [this, &dlg](const QString& id, long long expiresAt, const QString& period,
                                  bool autoRefresh) {
      Project* pr = w.findProject(id.toStdString());
      if (!pr) return;
      pr->meta.expiresAt = expiresAt;
      pr->meta.refreshPeriod = period.toStdString();
      pr->meta.autoRefresh = autoRefresh;
      fileStore::saveProjects(w.projectList);
      dlg.setProjects(w.projectList);
      const QString shown = support::shortName(QString::fromStdString(pr->meta.name));
      w.notify->success(expiresAt == 0 ? QString("\"%1\" is kept forever").arg(shown)
                                               : QString("\"%1\" expiration updated").arg(shown));
    });
    if (w.execMaybePopover(dlg) != QDialog::Accepted) return;

    typedef ProjectsDialog::Action Action;
    // Open is already confirmed IN-DIALOG (ProjectsDialog::finishOpen).
    if (dlg.getAction() == Action::OPEN) {
      w.loadProjectIntoCanvas(dlg.getSelectedId());
    } else if (dlg.getAction() == Action::OPEN_REMOTE) {
      openServerProject(dlg.getSelectedServerUrl(), dlg.getSelectedId());
    } else if (dlg.getAction() == Action::OPEN_IN_NEW_WINDOW) {
      openProjectInNewWindow(dlg.getSelectedId());
    } else if (dlg.getAction() == Action::MOVE_TO_SERVER) {
      if (w.projectOpenInOtherWindow(dlg.getSelectedId())) {
        w.notify->error("That project is open in another window — close it there first");
        return;
      }
      w.projectTransfer->moveLocalProjectToServer(dlg.getSelectedServerUrl(), dlg.getSelectedId());
    } else if (dlg.getAction() == Action::COPY_TO_SERVER) {
      w.projectTransfer->copyLocalProjectToServer(dlg.getSelectedServerUrl(), dlg.getSelectedId(), dlg.getNewName());
    } else if (dlg.getAction() == Action::MOVE_TO_LOCAL) {
      // Move-to-local is allowed with a peer open: the server delete just ends their live link.
      w.projectTransfer->moveServerProjectToLocal(dlg.getSelectedServerUrl(), dlg.getSelectedId());
    } else if (dlg.getAction() == Action::MAKE_LOCAL_COPY) {
      w.projectTransfer->makeLocalCopyOfServerProject(dlg.getSelectedServerUrl(), dlg.getSelectedId(), dlg.getNewName());
    } else if (dlg.getAction() == Action::BATCH_MOVE_TO_SERVER) {
      for (const auto& pr : dlg.batchItems()) w.projectTransfer->moveLocalProjectToServer(dlg.getSelectedServerUrl(), pr.first);
    } else if (dlg.getAction() == Action::BATCH_COPY_TO_SERVER) {
      for (const auto& pr : dlg.batchItems()) w.projectTransfer->copyLocalProjectToServer(dlg.getSelectedServerUrl(), pr.first, QString());
    } else if (dlg.getAction() == Action::BATCH_MOVE_TO_LOCAL) {
      for (const auto& pr : dlg.batchItems()) w.projectTransfer->moveServerProjectToLocal(pr.second, pr.first);
    } else if (dlg.getAction() == Action::BATCH_COPY_TO_LOCAL) {
      // Each import is async; refresh + notify once the last one lands.
      const auto items = dlg.batchItems();
      const int total = static_cast<int>(items.size());
      if (total == 0) {
        w.refreshActions();
        SiblingWindows::refreshDockMenu(w.projectList);
        w.notify->success(QStringLiteral("Made 0 local copy(ies)"));
      } else {
        auto remaining = std::make_shared<int>(total);
        QPointer<MainWindow> self(&w);
        for (const auto& pr : items) {
          w.projectTransfer->importServerProjectToLocal(
              pr.second, pr.first, /*removeFromServer=*/false, QString(),
              [this, self, remaining, total](bool, QString) {
                if (--*remaining == 0 && self) {
                  w.refreshActions();
                  SiblingWindows::refreshDockMenu(w.projectList);
                  w.notify->success(QString("Made %1 local copy(ies)").arg(total));
                }
              });
        }
      }
    } else if (dlg.getAction() == Action::SET_COLOR) {
      // Capture the selection by value — `dlg` dies when openProjects returns, before the async PUT completes.
      const QString cid = dlg.getSelectedId();
      const QString csrv = dlg.getSelectedServerUrl();
      const QString ccol = dlg.getSelectedColor();
      QPointer<MainWindow> self(&w);
      setProjectColorById(cid, csrv, ccol, [this, self, cid, csrv, ccol](bool ok) {
        if (!self || !ok) return;
        if (csrv.isEmpty() && w.activeProjectId == cid) {
          w.projectTitle->updateProjectTitle();
        } else if (!csrv.isEmpty() && w.remote.session->getLink().id == cid
                   && w.remote.session->getLink().address == csrv) {
          w.remote.session->getLink().color = normalizeProjectColor(ccol).value_or(QString());
          w.projectTitle->updateProjectTitle();
        }
      });
    } else if (dlg.getAction() == Action::RENAME) {
      renameProjectById(dlg.getSelectedId(), dlg.getNewName());
    } else if (dlg.getAction() == Action::NEW) {
      if (w.incognito) {  // an explicit promotion out of incognito, not an app-side write
        const QString promoted = w.promoteIncognitoToLocal(dlg.getNewName());
        w.notify->success(promoted.isEmpty()
                             ? QStringLiteral("Nothing to save yet")
                             : QStringLiteral("Left incognito — saved \"%1\"")
                                   .arg(support::shortName(promoted)));
        return;
      }
      createProject(dlg.getNewName());
    } else if (dlg.getAction() == Action::NEW_BLANK) {
      w.newBlankImage();
    }
  }
}  // namespace stencil::gui
