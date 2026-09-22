#include "MainWindow.hpp"
#include "Notifications.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "launchOptions.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "ProjectsDialog.hpp"
#include "connectionStore.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include "LiveFeed.hpp"
#include "ServerClient.hpp"
#include "../../support/control/swap/controlSwap.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/icon/iconMotion.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QTimer>
#include <QUrl>

// CLI launch options and the stencil:// deep link.

namespace stencil::gui {

  // The desktop twin of the browser's URL launch (applyExternalLaunch + applyProjectDeepLink). Runs after show(): resolution is async.
  void MainWindow::applyLaunchOptions(const LaunchOptions& opts) {
    if (opts.empty()) return;

    // Incognito set FIRST so it gates the theme persist and every later write.
    if (opts.incognito && opts.project.isEmpty() && actIncognito->isEnabled())
      actIncognito->setChecked(true);  // drives incognito via its toggled slot

    if (opts.hasTheme) {
      settings.themeMode = (opts.theme == "dark") ? "dark" : "light";
      applySettings(settings, /*persist=*/true);
    }

    // Priority: --project > stencil:// server reference > --src > positional file.
    if (!opts.project.isEmpty()) {
      if (!openProjectByName(opts.project))
        notify->error(QString("No project named \"%1\"").arg(opts.project));
    } else if (!opts.serverUrl.isEmpty() && !opts.serverProjectId.isEmpty()) {
      // Queued so the connect + download run after show().
      const QString url = opts.serverUrl, id = opts.serverProjectId;
      const bool incog = opts.incognito;
      QTimer::singleShot(0, this, [this, url, id, incog] {
        openServerLaunch(url, id, incog);
      });
    } else if (!opts.src.isEmpty()) {
      pendingLaunchLayout = opts.layout;  // applied after the image loads
      pendingLaunchLayoutJson = opts.layoutJson;
      // Quick-crop override from the "Open in new window" handoff; consumed by applyQuickCrop().
      if (opts.hasCropOverride)
        pendingCrop =
            opts.cropToPage
                ? QuickCropOpts{QuickCropOpts::Mode::PAGE, opts.cropAlbum, opts.cropPage,
                                {opts.cropX, opts.cropY, opts.cropW, opts.cropH}}
                : QuickCropOpts{QuickCropOpts::Mode::NONE, false, QString()};
      openImageSource(opts.src, opts.frame, opts.srcFallbacks);
    } else if (!opts.file.isEmpty()) {
      pendingLaunchLayout = opts.layout;
      openPathFromOS(opts.file, opts.frame);
    }

    // Queued so it runs after a primary load has been kicked off.
    if (opts.projects) QTimer::singleShot(0, this, &MainWindow::openProjects);
  }

  // A stencil:// deep link on a RUNNING app (macOS QFileOpenEvent url).
  void MainWindow::openStencilUrl(const QUrl& url) {
    const LaunchOptions opts = parseStencilUrl(url);
    if (opts.empty()) {
      notify->error("Could not read the stencil:// link");
      return;
    }
    applyLaunchOptions(opts);
  }

  void MainWindow::openServerLaunch(const QString& serverUrl, const QString& id,
                                    bool incognito) {
    // normalizeBase yields "" on junk.
    const QString url = stencil::net::ServerClient::normalizeBase(serverUrl);
    if (url.isEmpty()) {
      notify->error("Bad server URL in the link");
      return;
    }
    if (incognito && actIncognito->isEnabled()) actIncognito->setChecked(true);
    auto* mgr = ensureConnections();
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
        spec.title = tr("Open shared project");
        spec.message = QString("This link opens a shared project on %1.\nConnect to that server?")
                           .arg(url);
        spec.confirmLabel = tr("Connect");
        spec.confirmIcon = QStringLiteral("server");
        if (!confirmModal(this, spec)) return;
      }
      QPointer<MainWindow> self(this);
      mgr->connectToAsync(url, token, [this, self, url, id, incognito](bool ok, QString err) {
        if (!self) return;
        if (!ok) {
          // The normal connect path: surface the failure and open the Servers dialog.
          notify->error(QString("Could not connect to %1 — %2").arg(url, err));
          openConnections();
          return;
        }
        warnInsecureConnections();
        openServerProject(url, id, /*silent=*/false, /*link=*/!incognito);
      }, kind);
      return;
    }
    openServerProject(url, id, /*silent=*/false, /*link=*/!incognito);
  }

}  // namespace stencil::gui
