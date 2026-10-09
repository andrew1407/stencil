#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "SiblingWindows.hpp"
#include "ProjectFlows.hpp"
#include "mainWindowHelpers.hpp"
#include "Notifications.hpp"
#include "ProjectTitleController.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "displayName.hpp"

// The project metadata: the description and keywords dialogs, and renaming a project in the
// registry by id (the Projects window's rename and the name row's commit).

namespace stencil::gui {

  // Written back through the Projects window's store path (DescriptionDialog::apply ≙ commitRowEdit).
  void ProjectFlows::openDescription() {
    Project* pr = w.incognito ? nullptr : w.findProject(w.activeProjectId.toStdString());
    if (!pr) { w.notify->info("Save the project first to add a description"); return; }
    const QString current = QString::fromStdString(pr->meta.description);
    DescriptionDialog dlg(current, &w);
    if (w.execMaybePopover(dlg, w.acts.description) != QDialog::Accepted) return;
    const QString text = dlg.text();
    if (text == current) return;
    setProjectDescriptionById(w.activeProjectId, QString(), text);
  }

  // Same way (KeywordsDialog::apply ≙ commitRowEdit).
  void ProjectFlows::openKeywords() {
    Project* pr = w.incognito ? nullptr : w.findProject(w.activeProjectId.toStdString());
    if (!pr) { w.notify->info("Save the project first to add keywords"); return; }
    QStringList current;
    for (const auto& k : pr->meta.keywords) current << QString::fromStdString(k);
    KeywordsDialog dlg(current, &w);
    if (w.execMaybePopover(dlg, w.acts.keywords) != QDialog::Accepted) return;
    const QStringList next = dlg.keywords();
    if (next == current) return;
    setProjectKeywordsById(w.activeProjectId, QString(), next);
  }

  void ProjectFlows::setProjectDescriptionById(const QString& id, const QString& serverUrl,
                                               const QString& text) {
    if (serverUrl.isEmpty()) {
      if (!DescriptionDialog::apply(w.projectList, id, text, nowMs())) return;
      SharedState::instance().saveProjects(&w);
      w.notify->success(text.isEmpty() ? "Description cleared" : "Description saved");
      return;
    }
    putServerMeta(serverUrl, id, QStringLiteral("Description"),
                  [id, text](stencil::net::ServerClient* c, qint64 version,
                             std::function<void(bool, qint64, bool)> cb) {
                    c->updateProjectDescriptionAsync(id, text, version, std::move(cb));
                  });
  }

  void ProjectFlows::setProjectKeywordsById(const QString& id, const QString& serverUrl,
                                            const QStringList& keywords) {
    if (serverUrl.isEmpty()) {
      if (!KeywordsDialog::apply(w.projectList, id, keywords, nowMs())) return;
      SharedState::instance().saveProjects(&w);
      w.notify->success(keywords.isEmpty() ? "Keywords cleared" : "Keywords saved");
      return;
    }
    putServerMeta(serverUrl, id, QStringLiteral("Keywords"),
                  [id, keywords](stencil::net::ServerClient* c, qint64 version,
                                 std::function<void(bool, qint64, bool)> cb) {
                    c->updateProjectKeywordsAsync(id, keywords, version, std::move(cb));
                  });
  }

  // The row the Projects window lists re-reads itself on its poll; the linked project adopts
  // only its own version bump, so a peer's edit meanwhile still reloads.
  void ProjectFlows::putServerMeta(const QString& serverUrl, const QString& id, const QString& what,
                                   std::function<void(stencil::net::ServerClient*, qint64,
                                                      std::function<void(bool, qint64, bool)>)> put) {
    stencil::net::ServerClient* c = w.remote.session->requireClient(serverUrl);
    if (!c) return;
    const qint64 before = w.remote.session->getLink().version;
    QPointer<MainWindow> self(&w);
    w.remote.session->putVersionGuardedAsync(
        c, id,
        [c, put](qint64 version, std::function<void(bool, qint64, bool)> cb) { put(c, version, std::move(cb)); },
        [this, self, c, id, serverUrl, what, before](bool ok, qint64 newVersion) {
          if (!self) return;
          if (!ok) {
            w.notify->error(QString("%1 update failed: %2").arg(what, c->lastError()));
            return;
          }
          RemoteLink& link = w.remote.session->getLink();
          if (link.id == id && link.address == serverUrl) adoptOwnFileVersion(link, before, newVersion);
          w.notify->success(QString("%1 saved").arg(what));
        });
  }

  bool ProjectFlows::renameProjectById(const QString& id, const QString& rawName) {
    const QString name = rawName.trimmed();
    Project* pr = w.findProject(id.toStdString());
    if (!pr) return false;
    const auto check = w.checkProjectName(name, id);
    if (!check.ok) {
      w.notify->error(QString::fromStdString(check.reason));
      return false;
    }
    pr->meta.name = name.toStdString();
    // Downloads use projectBaseName(), so there is no separate image name.
    SharedState::instance().saveProjects(&w);
    SiblingWindows::refreshDockMenu(w.projectList);
    if (w.activeProjectId == id) w.projectTitle->updateProjectTitle();
    w.notify->success(QString("Renamed to \"%1\"").arg(support::shortName(name)));
    return true;
  }

}  // namespace stencil::gui
