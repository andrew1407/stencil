#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "SiblingWindows.hpp"
#include "ProjectFlows.hpp"
#include "stamps.hpp"
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
    Project* pr = host.incognito ? nullptr : host.findProject(host.activeProjectId.toStdString());
    if (!pr) { w.notify->info("Save the project first to add a description"); return; }
    const QString current = QString::fromStdString(pr->meta.description);
    DescriptionDialog dlg(current, w.window);
    if (w.execMaybePopover(dlg, w.acts.description) != QDialog::Accepted) return;
    const QString text = dlg.text();
    if (text == current) return;
    setProjectDescriptionById(host.activeProjectId, QString(), text);
  }

  // Same way (KeywordsDialog::apply ≙ commitRowEdit).
  void ProjectFlows::openKeywords() {
    Project* pr = host.incognito ? nullptr : host.findProject(host.activeProjectId.toStdString());
    if (!pr) { w.notify->info("Save the project first to add keywords"); return; }
    QStringList current;
    for (const auto& k : pr->meta.keywords) current << QString::fromStdString(k);
    KeywordsDialog dlg(current, w.window);
    if (w.execMaybePopover(dlg, w.acts.keywords) != QDialog::Accepted) return;
    const QStringList next = dlg.keywords();
    if (next == current) return;
    setProjectKeywordsById(host.activeProjectId, QString(), next);
  }

  void ProjectFlows::setProjectDescriptionById(const QString& id, const QString& serverUrl,
                                               const QString& text, MetaLanded landed) {
    setProjectMetaById(
        id, serverUrl, QStringLiteral("Description"), text.isEmpty(),
        [this, id, text] { return DescriptionDialog::apply(host.projectList, id, text, nowMs()); },
        [id, text](stencil::net::ServerClient* c, qint64 version, std::function<void(bool, qint64, bool)> cb) {
          c->updateProjectDescriptionAsync(id, text, version, std::move(cb));
        },
        std::move(landed));
  }

  void ProjectFlows::setProjectKeywordsById(const QString& id, const QString& serverUrl,
                                            const QStringList& keywords, MetaLanded landed) {
    setProjectMetaById(
        id, serverUrl, QStringLiteral("Keywords"), keywords.isEmpty(),
        [this, id, keywords] { return KeywordsDialog::apply(host.projectList, id, keywords, nowMs()); },
        [id, keywords](stencil::net::ServerClient* c, qint64 version, std::function<void(bool, qint64, bool)> cb) {
          c->updateProjectKeywordsAsync(id, keywords, version, std::move(cb));
        },
        std::move(landed));
  }

  void ProjectFlows::setProjectMetaById(const QString& id, const QString& serverUrl, const QString& what,
                                        bool cleared, const std::function<bool()>& applyLocal, MetaPut put,
                                        MetaLanded landed) {
    if (!serverUrl.isEmpty()) {
      putServerMeta(serverUrl, id, what, std::move(put), {}, std::move(landed));
      return;
    }
    if (!applyLocal()) return;
    SharedState::instance().saveProjects(w.window);
    w.notify->success(what + (cleared ? QStringLiteral(" cleared") : QStringLiteral(" saved")));
  }

  // The linked project adopts only its own version bump, so a peer's edit meanwhile still reloads.
  void ProjectFlows::putServerMeta(const QString& serverUrl, const QString& id, const QString& what,
                                   MetaPut put, const QString& saved, MetaLanded landed) {
    stencil::net::ServerClient* c = w.remote.session->requireClient(serverUrl);
    if (!c) {
      if (landed) landed(false, 0);
      return;
    }
    QPointer<QWidget> self(w.window);
    w.remote.session->putMetaAsync(
        c, serverUrl, id,
        [c, put](qint64 version, std::function<void(bool, qint64, bool)> cb) { put(c, version, std::move(cb)); },
        {},
        [this, self, c, what, saved, landed](bool ok, qint64 newVersion) {
          if (!self) return;
          if (ok) w.notify->success(saved.isEmpty() ? QString("%1 saved").arg(what) : saved);
          else w.notify->error(QString("%1 update failed: %2").arg(what, c->lastError()));
          if (landed) landed(ok, newVersion);
        });
  }

  bool ProjectFlows::renameProjectById(const QString& id, const QString& rawName) {
    const QString name = rawName.trimmed();
    Project* pr = host.findProject(id.toStdString());
    if (!pr) return false;
    const auto check = host.checkProjectName(name, id);
    if (!check.ok) {
      w.notify->error(QString::fromStdString(check.reason));
      return false;
    }
    pr->meta.name = name.toStdString();
    // Downloads use projectBaseName(), so there is no separate image name.
    SharedState::instance().saveProjects(w.window);
    SiblingWindows::refreshDockMenu(host.projectList);
    if (host.activeProjectId == id) w.projectTitle->updateProjectTitle();
    w.notify->success(QString("Renamed to \"%1\"").arg(support::shortName(name)));
    return true;
  }

}  // namespace stencil::gui
