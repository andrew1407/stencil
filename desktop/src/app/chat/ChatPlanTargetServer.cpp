#include "ChatPlanTarget.hpp"

#include "MainWindow.hpp"
#include "../../support/modal/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "mainWindowHelpers.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "../../canvas/CanvasWidget.hpp"
#include "../../net/connectionStore.hpp"
#include "../../net/ServerClient.hpp"
#include "../../support/displayName.hpp"
#include "../../support/notify/Notifications.hpp"
#include "../../support/theme/theme.hpp"

#include <QEventLoop>
#include <QTimer>

namespace stencil::gui {
  bool ChatPlanTarget::connectServer(const QString& server, QString* err) {
    // Resolve ONLY against the user's SAVED servers; plans never carry tokens or hosts (contract §10).
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
    auto kind = stencil::net::ServerClient::CredentialKind::NONE;
    for (const auto& s : saved)
      if (s.url == url) { token = s.token; kind = stencil::net::ServerClient::kindFromTag(s.kind); break; }
    // The executor runs ops in order, so this waits out the handshake in the bounded local loop chatLoadSource uses.
    QString cerr;
    bool done = false, ok = false;
    QEventLoop loop;
    QTimer::singleShot(20000, &loop, [&loop] { loop.quit(); });   // never hang the plan
    w.ensureConnections()->connectToAsync(url, token, [&](bool o, QString e) {
      ok = o; cerr = std::move(e); done = true; loop.quit();
    }, kind);
    if (!done) loop.exec();
    if (!ok) {
      if (err) *err = QStringLiteral("connect: %1").arg(cerr.isEmpty() ? QStringLiteral("timed out") : cerr);
      return false;
    }
    w.notify->success(QStringLiteral("Connected to %1").arg(url));
    return true;
  }
  bool ChatPlanTarget::disconnectServer(const QString& server, QString* err) {
    const QStringList live = w.connections ? w.connections->urls() : QStringList();
    const QString url = llm::resolveServerRef(server, live);
    if (url.isEmpty()) {
      if (err)
        *err = QStringLiteral("disconnect: unknown server \"%1\" — not a live connection")
                   .arg(server);
      return false;
    }
    w.connections->disconnectFrom(url);
    w.notify->info(QStringLiteral("Disconnected from %1").arg(url));
    return true;
  }

  // §10 openUrl: the SAME async path as the dialog's "open here"; the executor awaits it (chatLoadSource).
  bool ChatPlanTarget::openUrl(const QString& url, bool incognito, QString* err) {
    // Say what happened in OUR words — a silent download reads as "nothing happened".
    w.notify->info(QStringLiteral("Opening %1%2")
                         .arg(url, incognito ? QStringLiteral(" (incognito)") : QString()));
    QString why;
    if (w.chatLoadSource(url, incognito, &why)) return true;
    if (err) *err = QStringLiteral("openUrl: %1").arg(why);
    return false;
  }
  // §10 openFile: the same await for a LOCAL path; a .stencil or .json takes its own path.
  bool ChatPlanTarget::openFile(const QString& path, QString* err) {
    return w.chatOpenFile(path, err);
  }
  // §10 copy: the toolbar's "Copy Image to Clipboard" path (DataExportController).
  bool ChatPlanTarget::copyImage(QString*) {
    w.dataExport->copyImageToClipboard();
    return true;
  }
  bool ChatPlanTarget::copyLayout(QString*) {
    w.dataExport->copyLayout();
    return true;
  }
  bool ChatPlanTarget::hasDrawnLines() const { return !w.canvas->getLines().empty(); }
  // Shared §10 resolution: exact name, else unique case-insensitive prefix; nullptr + *note on a miss.
  const Project* ChatPlanTarget::resolveLocalProject(const QString& name,
                                                     QString* note) const {
    std::vector<const Project*> exact, prefixed;
    const std::string want = name.toStdString();
    for (const auto& p : w.projectList) {
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
  // §10 removeProject: saved LOCAL projects (or the ACTIVE one for current:true), then the dialog's Delete flow. A decline is a note.
  bool ChatPlanTarget::removeProjectNamed(const QString& name, bool current,
                                          QString* note) {
    QString id, nm;
    if (current) {
      if (w.activeProjectId.isEmpty()) {
        if (!w.canvas->hasImage()) {
          *note = QStringLiteral("no saved project is open right now");
          return true;
        }
        ConfirmSpec spec;
        spec.title = "Remove image";
        spec.message = QString("Nothing is saved here — remove this editor's image and "
                               "its lines?");
        spec.confirmIcon = QStringLiteral("trash");
        spec.danger = true;
        if (!confirmModal(&w, spec)) {
          *note = QStringLiteral("removal canceled");
          return true;
        }
        w.resetToBlankEditor();
        w.notify->success("Editor cleared");
        return true;
      }
      id = w.activeProjectId;
      nm = support::shortName(w.activeProjectName());
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
    if (!confirmModal(&w, spec)) {
      *note = QStringLiteral("removal canceled");
      return true;
    }
    w.eraseLocalProject(id);
    fileStore::saveProjects(w.projectList);
    w.refreshActions();
    w.refreshDockMenu();
    w.notify->info("Project deleted");
    return true;
  }
  // §10 renameProject: the commitProjectName path, pre-validated so a duplicate surfaces the store's reason.
  bool ChatPlanTarget::renameActiveProject(const QString& name, QString* note) {
    const bool remote = !w.remoteSession->getLink().id.isEmpty();
    if (!remote && w.activeProjectId.isEmpty()) {
      *note = QStringLiteral("no active saved project to rename");
      return true;
    }
    if (!remote) {
      const auto check = w.checkProjectName(name, w.activeProjectId);
      if (!check.ok) {
        *note = QString::fromStdString(check.reason);
        return true;
      }
    }
    w.nameBar.field->setText(name);
    w.commitProjectName();
    return true;
  }
  bool ChatPlanTarget::setProjectColor(const QString& color, QString* note) {
    if (w.remoteSession->getLink().id.isEmpty() && w.activeProjectId.isEmpty()) {
      *note = QStringLiteral("no active project — open or save one first");
      return true;
    }
    w.setActiveProjectColor(color);
    return true;
  }
}  // namespace stencil::gui

