// The projects dialog: opening it, and every action it can come back with — open, remove, rename,
// recolour, expiry and the batch transfers between local storage and a server.
#include "mainWindowShellParts.hpp"

namespace stencil::gui {

  void MainWindow::openProjects() {
    // Expiry sweep (one week).
    projectsStore.clearAll();
    std::vector<core::ProjectMeta> metas;
    for (const auto& pr : projectList) metas.push_back(pr.meta);
    projectsStore.load(metas);
    const auto expired = projectsStore.sweepExpired(nowMs());
    if (!expired.empty()) {
      projectList.erase(
          std::remove_if(projectList.begin(), projectList.end(),
                         [&](const Project& p) {
                           return std::find(expired.begin(), expired.end(),
                                            p.meta.id) != expired.end();
                         }),
          projectList.end());
      // Not gated by incognito: other saved projects, not the incognito editor's content.
      fileStore::saveProjects(projectList);
    }

    ProjectsDialog dlg(projectList, nowMs(), connections, buildProjectThumbs(),
                       this, activeProjectId, accentPrimary(settings.accentColor));
    // No project open: the list pins this window as "Temporary (unsaved)". Re-asked per removal — deleting the OPEN project
    // resets this window to a blank editor — and it travels WITH the list so the batch bar and row repaint together.
    const auto unsavedSession = [this] {
      return activeProjectId.isEmpty() && remoteSession->getLink().id.isEmpty();
    };
    dlg.setTemporary(unsavedSession(), incognito);
    dlg.setDragZones(projectZones);   // the main-window drag-out zone overlay (open/new-window/remove)
    // Handled WHILE the dialog is up: it confirms itself, we remove, it repaints.
    connect(&dlg, &ProjectsDialog::clearAllRequested, this, [this, &dlg, unsavedSession] {
      const int n = static_cast<int>(projectList.size());
      const bool hadActive = !activeProjectId.isEmpty();
      projectList.clear();
      if (hadActive) resetToBlankEditor();   // the open one went with them
      fileStore::saveProjects(projectList);
      refreshActions();
      refreshDockMenu();
      // Rows are still scattering; rebuild once the motes have landed (browser: beginRemoval).
      QPointer<ProjectsDialog> live(&dlg);
      QTimer::singleShot(DisintegrateOverlay::DUST_MS, this, [this, live, unsavedSession] {
        if (live) live->setProjects(projectList, unsavedSession(), incognito);
      });
      notify->success(QString("Cleared %1 local project(s)").arg(n));
    });
    // Same stay-open pattern: remove, then repaint once the dust lands.
    connect(&dlg, &ProjectsDialog::removeRequested, this,
            [this, &dlg, unsavedSession](const QVector<QPair<QString, QString>>& items) {
      QPointer<ProjectsDialog> live(&dlg);
      const bool single = items.size() == 1 && items.first().second.isEmpty();
      // A project open in another window cannot be removed (browser "open in another tab" guard).
      if (single && projectOpenInOtherWindow(items.first().first)) {
        notify->error("That project is open in another window — close it there first");
        if (live) live->setProjects(projectList);
        return;
      }
      for (const auto& pr : items) {
        if (pr.second.isEmpty()) {
          eraseLocalProject(pr.first);
        } else if (auto* c = connections ? connections->find(pr.second) : nullptr) {
          c->deleteProjectAsync(pr.first, [](bool) {});  // fire-and-forget; list refresh is independent
        }
      }
      fileStore::saveProjects(projectList);
      refreshActions();
      refreshDockMenu();  // drop it from the Dock "recent" list
      if (single) notify->info("Project deleted");
      QTimer::singleShot(DisintegrateOverlay::DUST_MS, this, [this, live, unsavedSession] {
        if (live) live->setProjects(projectList, unsavedSession(), incognito);
      });
    });
    // Inline rename: same stay-open pattern.
    connect(&dlg, &ProjectsDialog::renameRequested, this,
            [this, &dlg](const QString& id, const QString& name) {
      renameProjectById(id, name);
      dlg.setProjects(projectList);
    });
    // "Set expiration" / "Open in another app": the list stays up.
    const QString botUser = settings.telegramBotUsername.trimmed();
    const bool browserTarget = !settings.browserBaseUrl.trimmed().isEmpty();
    dlg.setOpenInAvailable(browserTarget, browserTarget || !botUser.isEmpty());
    connect(&dlg, &ProjectsDialog::openInRequested, this,
            [this](const QString& id, const QString& serverUrl, const QRect& closeRect) {
      openInAnotherAppFor(id, serverUrl, closeRect);
    });
    connect(&dlg, &ProjectsDialog::expirationRequested, this,
            [this, &dlg](const QString& id, long long expiresAt, const QString& period,
                         bool autoRefresh) {
      Project* pr = findProject(id.toStdString());
      if (!pr) return;
      pr->meta.expiresAt = expiresAt;
      pr->meta.refreshPeriod = period.toStdString();
      pr->meta.autoRefresh = autoRefresh;
      fileStore::saveProjects(projectList);
      dlg.setProjects(projectList);
      const QString shown = support::shortName(QString::fromStdString(pr->meta.name));
      notify->success(expiresAt == 0 ? QString("\"%1\" is kept forever").arg(shown)
                                      : QString("\"%1\" expiration updated").arg(shown));
    });
    if (execMaybePopover(dlg) != QDialog::Accepted) return;

    typedef ProjectsDialog::Action Action;
    // Open is already confirmed IN-DIALOG (ProjectsDialog::finishOpen).
    if (dlg.getAction() == Action::OPEN) {
      loadProjectIntoCanvas(dlg.getSelectedId());
    } else if (dlg.getAction() == Action::OPEN_REMOTE) {
      openServerProject(dlg.getSelectedServerUrl(), dlg.getSelectedId());
    } else if (dlg.getAction() == Action::OPEN_IN_NEW_WINDOW) {
      openProjectInNewWindow(dlg.getSelectedId());
    } else if (dlg.getAction() == Action::MOVE_TO_SERVER) {
      if (projectOpenInOtherWindow(dlg.getSelectedId())) {
        notify->error("That project is open in another window — close it there first");
        return;
      }
      projectTransfer->moveLocalProjectToServer(dlg.getSelectedServerUrl(), dlg.getSelectedId());
    } else if (dlg.getAction() == Action::COPY_TO_SERVER) {
      projectTransfer->copyLocalProjectToServer(dlg.getSelectedServerUrl(), dlg.getSelectedId(), dlg.getNewName());
    } else if (dlg.getAction() == Action::MOVE_TO_LOCAL) {
      // Move-to-local is allowed with a peer open: the server delete just ends their live link.
      projectTransfer->moveServerProjectToLocal(dlg.getSelectedServerUrl(), dlg.getSelectedId());
    } else if (dlg.getAction() == Action::MAKE_LOCAL_COPY) {
      projectTransfer->makeLocalCopyOfServerProject(dlg.getSelectedServerUrl(), dlg.getSelectedId(), dlg.getNewName());
    } else if (dlg.getAction() == Action::BATCH_MOVE_TO_SERVER) {
      for (const auto& pr : dlg.batchItems()) projectTransfer->moveLocalProjectToServer(dlg.getSelectedServerUrl(), pr.first);
    } else if (dlg.getAction() == Action::BATCH_COPY_TO_SERVER) {
      for (const auto& pr : dlg.batchItems()) projectTransfer->copyLocalProjectToServer(dlg.getSelectedServerUrl(), pr.first, QString());
    } else if (dlg.getAction() == Action::BATCH_MOVE_TO_LOCAL) {
      for (const auto& pr : dlg.batchItems()) projectTransfer->moveServerProjectToLocal(pr.second, pr.first);
    } else if (dlg.getAction() == Action::BATCH_COPY_TO_LOCAL) {
      // Each import is async; refresh + notify once the last one lands.
      const auto items = dlg.batchItems();
      const int total = static_cast<int>(items.size());
      if (total == 0) {
        refreshActions();
        refreshDockMenu();
        notify->success(QStringLiteral("Made 0 local copy(ies)"));
      } else {
        auto remaining = std::make_shared<int>(total);
        QPointer<MainWindow> self(this);
        for (const auto& pr : items) {
          projectTransfer->importServerProjectToLocal(
              pr.second, pr.first, /*removeFromServer=*/false, QString(),
              [this, self, remaining, total](bool, QString) {
                if (--*remaining == 0 && self) {
                  refreshActions();
                  refreshDockMenu();
                  notify->success(QString("Made %1 local copy(ies)").arg(total));
                }
              });
        }
      }
    } else if (dlg.getAction() == Action::SET_COLOR) {
      // Capture the selection by value — `dlg` dies when openProjects returns, before the async PUT completes.
      const QString cid = dlg.getSelectedId();
      const QString csrv = dlg.getSelectedServerUrl();
      const QString ccol = dlg.getSelectedColor();
      QPointer<MainWindow> self(this);
      setProjectColorById(cid, csrv, ccol, [this, self, cid, csrv, ccol](bool ok) {
        if (!self || !ok) return;
        if (csrv.isEmpty() && activeProjectId == cid) {
          updateProjectTitle();
        } else if (!csrv.isEmpty() && remoteSession->getLink().id == cid
                   && remoteSession->getLink().address == csrv) {
          remoteSession->getLink().color = normalizeProjectColor(ccol).value_or(QString());
          updateProjectTitle();
        }
      });
    } else if (dlg.getAction() == Action::RENAME) {
      renameProjectById(dlg.getSelectedId(), dlg.getNewName());
    } else if (dlg.getAction() == Action::NEW) {
      if (incognito) {  // an explicit promotion out of incognito, not an app-side write
        const QString promoted = promoteIncognitoToLocal(dlg.getNewName());
        notify->success(promoted.isEmpty()
                             ? QStringLiteral("Nothing to save yet")
                             : QStringLiteral("Left incognito — saved \"%1\"")
                                   .arg(support::shortName(promoted)));
        return;
      }
      createProject(dlg.getNewName());
    } else if (dlg.getAction() == Action::NEW_BLANK) {
      newBlankImage();
    }
  }
}  // namespace stencil::gui
