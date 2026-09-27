#include "MainWindow.hpp"
#include "ProjectFlows.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include "modalChrome.hpp"

// Opening the project a launch named: a local one by name, or a server one by its link.

namespace stencil::gui {

  void ProjectFlows::openServerLaunch(const QString& serverUrl, const QString& id,
                                    bool incognito) {
    // normalizeBase yields "" on junk.
    const QString url = stencil::net::ServerClient::normalizeBase(serverUrl);
    if (url.isEmpty()) {
      w.notify->error("Bad server URL in the link");
      return;
    }
    if (incognito && w.acts.incognito->isEnabled()) w.acts.incognito->setChecked(true);
    auto* mgr = w.ensureConnections();
    if (!mgr->find(url)) {
      // Reuse the saved token for this origin; else connect tokenless and the server mints one.
      QString token;
      auto kind = stencil::net::ServerClient::CredentialKind::NONE;
      bool known = false;
      for (const auto& s : stencil::net::connectionStore::loadSavedServers()) {
        if (stencil::net::ServerClient::normalizeBase(s.url) == url) {
          token = s.token;
          kind = stencil::net::ServerClient::kindFromTag(s.kind);
          known = true;
          break;
        }
      }
      // A drive-by stencil:// URL must not silently persist a connection to an unknown origin; known origins skip the prompt.
      if (!known) {
        ConfirmSpec spec;
        spec.title = MainWindow::tr("Open shared project");
        spec.message = QString("This link opens a shared project on %1.\nConnect to that server?")
                           .arg(url);
        spec.confirmLabel = MainWindow::tr("Connect");
        spec.confirmIcon = QStringLiteral("server");
        if (!confirmModal(&w, spec)) return;
      }
      QPointer<MainWindow> self(&w);
      mgr->connectToAsync(url, token, [this, self, url, id, incognito](bool ok, QString err) {
        if (!self) return;
        if (!ok) {
          // The normal connect path: surface the failure and open the Servers dialog.
          w.notify->error(QString("Could not connect to %1 — %2").arg(url, err));
          openConnections();
          return;
        }
        w.warnInsecureConnections();
        openServerProject(url, id, /*silent=*/false, /*link=*/!incognito);
      }, kind);
      return;
    }
    openServerProject(url, id, /*silent=*/false, /*link=*/!incognito);
  }

  bool ProjectFlows::openProjectByName(const QString& name) {
    const QString want = name.trimmed();
    for (const auto& p : w.projectList) {
      if (QString::fromStdString(p.meta.name).compare(want, Qt::CaseInsensitive) ==
          0)
        return w.loadProjectIntoCanvas(QString::fromStdString(p.meta.id));
    }
    return false;
  }

}  // namespace stencil::gui
