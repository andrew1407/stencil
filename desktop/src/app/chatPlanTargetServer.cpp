#include "chatPlanTarget.hpp"

#include "mainWindow.hpp"
#include "../support/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "mainWindowHelpers.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "../canvas/canvasWidget.hpp"
#include "../net/connectionStore.hpp"
#include "../net/serverClient.hpp"
#include "../support/displayName.hpp"
#include "../support/notifications.hpp"
#include "../support/theme.hpp"

#include <QEventLoop>
#include <QTimer>

namespace stencil::gui {
  bool ChatPlanTarget::connectServer(const QString& server, QString* err) {
    // Resolve ONLY against the user's SAVED servers (exact URL, else unique host); their
    // stored token authenticates — plans never carry tokens or hosts (contract §10).
    const auto saved = stencil::net::connectionStore::loadSavedServers();
    QStringList urls;
    for (const auto& s : saved) urls << s.url;
    const QString url = llm::resolveServerRef(server, urls);
    if (url.isEmpty()) {
      if (err)
        *err = QStringLiteral(
                   "connect: unknown server \"%1\" — only a server you have "
                   "already saved can be used")
                   .arg(server);
      return false;
    }
    QString token;
    auto kind = stencil::net::ServerClient::CredentialKind::None;
    for (const auto& s : saved)
      if (s.url == url) { token = s.token; kind = stencil::net::ServerClient::kindFromTag(s.kind); break; }
    // The executor runs its ops in order, so this one waits out the handshake — the same
    // bounded local loop chatLoadSource uses, not a blocking call inside the client.
    QString cerr;
    bool done = false, ok = false;
    QEventLoop loop;
    QTimer::singleShot(20000, &loop, [&loop] { loop.quit(); });   // never hang the plan
    w_.ensureConnections()->connectToAsync(url, token, [&](bool o, QString e) {
      ok = o; cerr = std::move(e); done = true; loop.quit();
    }, kind);
    if (!done) loop.exec();
    if (!ok) {
      if (err) *err = QStringLiteral("connect: %1").arg(cerr.isEmpty() ? QStringLiteral("timed out") : cerr);
      return false;
    }
    w_.notify_->success(QStringLiteral("Connected to %1").arg(url));
    return true;
  }
  bool ChatPlanTarget::disconnectServer(const QString& server, QString* err) {
    const QStringList live = w_.connections_ ? w_.connections_->urls() : QStringList();
    const QString url = llm::resolveServerRef(server, live);
    if (url.isEmpty()) {
      if (err)
        *err = QStringLiteral("disconnect: unknown server \"%1\" — not a live connection")
                   .arg(server);
      return false;
    }
    w_.connections_->disconnectFrom(url);
    w_.notify_->info(QStringLiteral("Disconnected from %1").arg(url));
    return true;
  }

  // §10 openUrl: the user-echo guard already ran in the executor; loading rides the SAME
  // async path as the dialog's "open here", and the executor awaits it (chatLoadSource).
  bool ChatPlanTarget::openUrl(const QString& url, bool incognito, QString* err) {
    // Say what happened in OUR words — a silent download plus a vague model
    // reply reads as "nothing happened".
    w_.notify_->info(QStringLiteral("Opening %1%2")
                         .arg(url, incognito ? QStringLiteral(" (incognito)") : QString()));
    QString why;
    if (w_.chatLoadSource(url, incognito, &why)) return true;
    if (err) *err = QStringLiteral("openUrl: %1").arg(why);
    return false;
  }
  // §10 openFile: the same await, pointed at a LOCAL path the user named (the
  // executor checked the echo rule); a .stencil or .json takes its own path.
  bool ChatPlanTarget::openFile(const QString& path, QString* err) {
    return w_.chatOpenFile(path, err);
  }
  // §10 copy: the SAME path as the toolbar's "Copy Image to Clipboard"
  // (DataExportController — image + filter, no overlay; it notifies too).
  bool ChatPlanTarget::copyImage(QString*) {
    w_.dataExport_->copyImageToClipboard();
    return true;
  }
  // §10 copy what:"layout": actCopyLayout_'s DataExportController path.
  bool ChatPlanTarget::copyLayout(QString*) {
    w_.dataExport_->copyLayout();
    return true;
  }
  bool ChatPlanTarget::hasDrawnLines() const { return !w_.canvas_->lines().empty(); }
  // Shared §10 resolution: a saved LOCAL project by exact name, else unique
  // case-insensitive prefix. nullptr + *note set on a miss/ambiguity.
  const Project* ChatPlanTarget::resolveLocalProject(const QString& name,
                                                     QString* note) const {
    std::vector<const Project*> exact, prefixed;
    const std::string want = name.toStdString();
    for (const auto& p : w_.projectList_) {
      if (p.meta.name == want) exact.push_back(&p);
      else if (QString::fromStdString(p.meta.name)
                   .startsWith(name, Qt::CaseInsensitive))
        prefixed.push_back(&p);
    }
    const auto& picks = exact.empty() ? prefixed : exact;
    if (picks.empty()) {
      *note = QStringLiteral("no saved project named \"%1\"").arg(name);
      return nullptr;
    }
    if (picks.size() > 1) {
      *note = QStringLiteral("\"%1\" matches %2 projects — use the full name")
                  .arg(name).arg(picks.size());
      return nullptr;
    }
    return picks.front();
  }
  // §10 removeProject: resolve among the saved LOCAL projects (or the ACTIVE one for
  // current:true, falling back to `clear` when nothing is saved but an image is open),
  // then the projects dialog's Delete flow. A miss/decline is a note, not a failure.
  bool ChatPlanTarget::removeProjectNamed(const QString& name, bool current,
                                          QString* note) {
    QString id, nm;
    if (current) {
      if (w_.activeProjectId_.isEmpty()) {
        if (!w_.canvas_->hasImage()) {
          *note = QStringLiteral("no saved project is open right now");
          return true;
        }
        ConfirmSpec spec;
        spec.title = "Remove image";
        spec.message = QString("Nothing is saved here — remove this editor's image and "
                               "its lines?");
        spec.confirmIcon = QStringLiteral("trash");
        spec.danger = true;
        if (!confirmModal(&w_, spec)) {
          *note = QStringLiteral("removal canceled");
          return true;
        }
        w_.resetToBlankEditor();
        w_.notify_->success("Editor cleared");
        return true;
      }
      id = w_.activeProjectId_;
      nm = support::shortName(w_.activeProjectName());
    } else {
      const Project* pick = resolveLocalProject(name, note);
      if (!pick) return true;  // *note says why
      id = QString::fromStdString(pick->meta.id);
      nm = support::shortName(QString::fromStdString(pick->meta.name));
    }
    ConfirmSpec spec;
    spec.title = "Remove project";
    spec.message = QString("Remove \"%1\"? This cannot be undone.").arg(nm);
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(&w_, spec)) {
      *note = QStringLiteral("removal canceled");
      return true;
    }
    w_.eraseLocalProject(id);
    fileStore::saveProjects(w_.projectList_);
    w_.refreshActions();
    w_.refreshDockMenu();
    w_.notify_->info("Project deleted");
    return true;
  }
  // §10 renameProject: the commitProjectName path (local rename or the server-linked live
  // push), pre-validated so a duplicate name surfaces the store's own reason as a note.
  bool ChatPlanTarget::renameActiveProject(const QString& name, QString* note) {
    const bool remote = !w_.remoteSession_->link().id.isEmpty();
    if (!remote && w_.activeProjectId_.isEmpty()) {
      *note = QStringLiteral("no active saved project to rename");
      return true;
    }
    if (!remote) {
      const auto check = w_.checkProjectName(name, w_.activeProjectId_);
      if (!check.ok) {
        *note = QString::fromStdString(check.reason);
        return true;
      }
    }
    w_.nameBar_.field->setText(name);
    w_.commitProjectName();
    return true;
  }
  // §10 projectColor: the project name-colour control ("" = theme accent).
  bool ChatPlanTarget::setProjectColor(const QString& color, QString* note) {
    if (w_.remoteSession_->link().id.isEmpty() && w_.activeProjectId_.isEmpty()) {
      *note = QStringLiteral("no active project — open or save one first");
      return true;
    }
    w_.setActiveProjectColor(color);
    return true;
  }
}  // namespace stencil::gui

