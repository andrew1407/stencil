// The Projects window's stay-open requests: the dialog confirms in place, the window acts and
// repaints it — clear all, remove, rename, expiry, "Open in", the copy menu.
#include "SharedState.hpp"
#include "SiblingWindows.hpp"
#include "MainWindow.hpp"
#include "displayName.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSession.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"

namespace stencil::gui {

  namespace {
    // How long a rebuild waits for the removal wipe (ProjectsDialog::retireRow's own gate).
    int rebuildAfterWipeMs() { return support::isDustAllowed() ? support::motionMs(DisintegrateOverlay::DUST_MS) : 0; }
  }  // namespace

  void ProjectFlows::wireProjectsRequests(ProjectsDialog& dlg,
                                          const std::function<bool()>& unsavedSession) {
    // Handled WHILE the dialog is up: it confirms itself, we remove, it repaints.
    QObject::connect(&dlg, &ProjectsDialog::clearAllRequested, w.window, [this, &dlg, unsavedSession] {
      const int n = static_cast<int>(host.projectList.size());
      const bool hadActive = !host.activeProjectId.isEmpty();
      host.projectList.clear();
      if (hadActive) resetToBlankEditor();   // the open one went with them
      SharedState::instance().saveProjects(w.window);
      w.refreshActions();
      SiblingWindows::refreshDockMenu(host.projectList);
      // Rows are still scattering; rebuild once the motes have landed (browser: beginRemoval).
      QPointer<ProjectsDialog> live(&dlg);
      QTimer::singleShot(rebuildAfterWipeMs(), w.window, [this, live, unsavedSession] {
        if (live) live->setProjects(host.projectList, unsavedSession(), host.incognito);
      });
      if (n) w.notify->success(clearedToast(n));
    });
    // Same stay-open pattern: remove, then repaint once the dust lands.
    QObject::connect(&dlg, &ProjectsDialog::removeRequested, w.window,
                     [this, &dlg, unsavedSession](const QVector<QPair<QString, QString>>& items) {
      QPointer<ProjectsDialog> live(&dlg);
      const bool single = items.size() == 1 && items.first().second.isEmpty();
      // A project open in another window cannot be removed (browser "open in another tab" guard).
      if (single && host.projectOpenInOtherWindow(items.first().first)) {
        w.notify->error("That project is open in another window — close it there first");
        if (live) live->setProjects(host.projectList);
        return;
      }
      for (const auto& pr : items) {
        if (pr.second.isEmpty()) {
          eraseLocalProject(pr.first);
        } else if (auto* c = w.remote.conns() ? w.remote.conns()->find(pr.second) : nullptr) {
          c->deleteProjectAsync(pr.first, [](bool) {});  // fire-and-forget; list refresh is independent
        }
      }
      SharedState::instance().saveProjects(w.window);
      w.refreshActions();
      SiblingWindows::refreshDockMenu(host.projectList);  // drop it from the Dock "recent" list
      w.notify->success(clearedToast(static_cast<int>(items.size())));
      QTimer::singleShot(rebuildAfterWipeMs(), w.window, [this, live, unsavedSession] {
        if (live) live->setProjects(host.projectList, unsavedSession(), host.incognito);
      });
    });
    // Inline rename: same stay-open pattern.
    QObject::connect(&dlg, &ProjectsDialog::renameRequested, w.window,
                     [this, &dlg](const QString& id, const QString& name) {
      renameProjectById(id, name);
      dlg.setProjects(host.projectList);
    });
    // A server row repaints from its own cached record once the PUT lands; a local one now.
    QObject::connect(&dlg, &ProjectsDialog::descriptionRequested, w.window,
                     [this, &dlg](const QString& id, const QString& server, const QString& text) {
      QPointer<ProjectsDialog> live(&dlg);
      setProjectDescriptionById(id, server, text, [live, id, server, text](bool ok, qint64 version) {
        if (ok && live) live->patchServerRow(id, server, version, [text](auto& sp) { sp.description = text; });
      });
      if (server.isEmpty()) dlg.setProjects(host.projectList);
    });
    QObject::connect(&dlg, &ProjectsDialog::keywordsRequested, w.window,
                     [this, &dlg](const QString& id, const QString& server, const QStringList& keywords) {
      QPointer<ProjectsDialog> live(&dlg);
      setProjectKeywordsById(id, server, keywords, [live, id, server, keywords](bool ok, qint64 version) {
        if (ok && live) live->patchServerRow(id, server, version, [keywords](auto& sp) { sp.keywords = keywords; });
      });
      if (server.isEmpty()) dlg.setProjects(host.projectList);
    });
    // "Set expiration" / "Open in another app": the list stays up.
    const QString botUser = w.settings.telegramBotUsername.trimmed();
    const bool browserTarget = !w.settings.browserBaseUrl.trimmed().isEmpty();
    dlg.setOpenInAvailable(browserTarget, browserTarget || !botUser.isEmpty());
    QObject::connect(&dlg, &ProjectsDialog::openInRequested, w.window, [this](const QString& id, const QString& serverUrl,
                     const QRect& closeRect) { openInAnotherAppFor(id, serverUrl, closeRect); });
    host.copy.wireProjectsList(dlg);   // "Make a copy" (app/project/copy/ProjectCopyFlow.cpp)
    QObject::connect(&dlg, &ProjectsDialog::expirationRequested, w.window,
                     [this, &dlg](const QString& id, long long expiresAt, const QString& period,
                                  bool autoRefresh) {
      Project* pr = host.findProject(id.toStdString());
      if (!pr) return;
      pr->meta.expiresAt = expiresAt;
      pr->meta.refreshPeriod = period.toStdString();
      pr->meta.autoRefresh = autoRefresh;
      SharedState::instance().saveProjects(w.window);
      dlg.setProjects(host.projectList);
      const QString shown = support::shortName(QString::fromStdString(pr->meta.name));
      w.notify->success(expiresAt == 0 ? QString("\"%1\" is kept forever").arg(shown)
                                               : QString("\"%1\" expiration updated").arg(shown));
    });
  }

}  // namespace stencil::gui
