#include "MainWindow.hpp"
#include "ProjectFlows.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include "modalChrome.hpp"

// Opening the project a launch named: a local one by name, or a server one by its link.

namespace stencil::gui {

  ProjectFlows::ConnectIntent ProjectFlows::connectIntent() {
    return {MainWindow::tr("Connect to server"), QStringLiteral("This link connects to %1.\nConnect to that server?")};
  }

  ProjectFlows::ConnectIntent ProjectFlows::openProjectIntent() {
    return {MainWindow::tr("Open shared project"),
            QStringLiteral("This link opens a shared project on %1.\nConnect to that server?")};
  }

  void ProjectFlows::connectLaunch(const QString& serverUrl, const QString& linkToken,
                                   std::function<void(const QString& url)> then,
                                   const ConnectIntent& intent) {
    // normalizeBase yields "" on junk.
    const QString url = stencil::net::ServerClient::normalizeBase(serverUrl);
    if (url.isEmpty()) {
      w.notify->error("Bad server URL in the link");
      return;
    }
    auto* mgr = w.ensureConnections();
    if (mgr->find(url)) {
      if (then) then(url);
      return;
    }
    // The link's own token first; else the saved one for this origin; else tokenless and the server mints one.
    QString token = linkToken;
    auto kind = stencil::net::ServerClient::CredentialKind::NONE;
    bool known = false;
    stencil::net::SavedServer saved;
    for (const auto& s : stencil::net::connectionStore::loadSavedServers()) {
      if (stencil::net::ServerClient::normalizeBase(s.url) == url) {
        if (token.isEmpty()) {
          token = s.token;
          kind = stencil::net::ServerClient::kindFromTag(s.kind);
        }
        known = true;
        saved = s;
        break;
      }
    }
    // A drive-by link must not silently persist a connection to an unknown origin; known origins skip the prompt.
    if (!known) {
      ConfirmSpec spec;
      spec.title = intent.title;
      spec.message = intent.message.arg(url);
      spec.confirmLabel = MainWindow::tr("Connect");
      spec.confirmIcon = QStringLiteral("server");
      if (!confirmModal(w.window, spec)) return;
    }
    QPointer<QWidget> self(w.window);
    auto landed = [this, self, url, then](bool ok, QString err) {
      if (!self) return;
      if (!ok) {
        // The normal connect path: surface the failure and open the Servers dialog.
        w.notify->error(QString("Could not connect to %1 — %2").arg(url, err));
        openConnections();
        return;
      }
      w.warnInsecureConnections();
      if (then) then(url);
    };
    // A link never replaces a saved credential: a differing token connects for this session only.
    if (known && !linkToken.isEmpty() && linkToken != saved.token) {
      mgr->connectSessionOnlyAsync(url, token, saved, landed);
      return;
    }
    mgr->connectToAsync(url, token, landed, kind);
  }

  void ProjectFlows::openServerLaunch(const QString& serverUrl, const QString& id,
                                    bool incognito, qint64 version) {
    if (incognito && w.acts.incognito->isEnabled()) w.acts.incognito->setChecked(true);
    connectLaunch(serverUrl, QString(), [this, id, incognito, version](const QString& url) {
      openServerProject(url, id, /*silent=*/false, /*link=*/!incognito, version);
    }, openProjectIntent());
  }

  bool ProjectFlows::openProjectByName(const QString& name) {
    const QString want = name.trimmed();
    for (const auto& p : host.projectList) {
      if (QString::fromStdString(p.meta.name).compare(want, Qt::CaseInsensitive) ==
          0)
        return host.loadProjectIntoCanvas(QString::fromStdString(p.meta.id));
    }
    return false;
  }

}  // namespace stencil::gui
