#include "mainWindow.hpp"
#include "notifications.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "projectsDialog.hpp"
#include "connectionStore.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "projectTransferController.hpp"
#include "liveFeed.hpp"
#include "serverClient.hpp"
#include "../support/controlSwap.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QTimer>
#include <QUrl>

// CLI launch options and the stencil:// deep link.

namespace stencil::gui {

  // Apply the parsed command-line options (gui/launchOptions.hpp). The desktop
  // counterpart of the browser's URL launch (applyExternalLaunch '#stencil=' +
  // applyProjectDeepLink '?open='). Runs after show(): the image/URL/video and
  // layout resolution is async, so it relies on the running event loop.
  void MainWindow::applyLaunchOptions(const LaunchOptions& opts) {
    if (opts.empty()) return;

    // Incognito is honored whenever we're NOT opening a saved project — a blank
    // incognito editor, or an incognito image. Set FIRST so it gates the theme
    // persist below and every write a subsequent load would trigger.
    if (opts.incognito && opts.project.isEmpty() && actIncognito_->isEnabled())
      actIncognito_->setChecked(true);  // drives incognito_ via its toggled slot

    // --theme dark|light: set + persist the default theme (persist is suppressed
    // while incognito, like every other settings write).
    if (opts.hasTheme) {
      settings_.themeMode = (opts.theme == "dark") ? "dark" : "light";
      applySettings(settings_, /*persist=*/true);
    }

    // Primary content priority: --project > a stencil:// server reference >
    // --src > a bare positional file.
    if (!opts.project.isEmpty()) {
      if (!openProjectByName(opts.project))
        notify_->error(QString("No project named \"%1\"").arg(opts.project));
    } else if (!opts.serverUrl.isEmpty() && !opts.serverProjectId.isEmpty()) {
      // Queued so the connect + download run on the event loop after show().
      const QString url = opts.serverUrl, id = opts.serverProjectId;
      const bool incog = opts.incognito;
      QTimer::singleShot(0, this, [this, url, id, incog] {
        openServerLaunch(url, id, incog);
      });
    } else if (!opts.src.isEmpty()) {
      pendingLaunchLayout_ = opts.layout;  // applied after the image loads
      pendingLaunchLayoutJson_ = opts.layoutJson;
      // A quick-crop override (Open-Image dialog "Open in new window" handoff): apply
      // the same page-aspect crop / whole-frame choice the user made in the preview,
      // instead of the default page-aspect auto-crop. Consumed by applyQuickCrop().
      if (opts.hasCropOverride)
        pendingCrop_ = opts.cropToPage
                           ? QuickCropOpts{QuickCropOpts::Mode::Page, opts.cropAlbum, opts.cropPage}
                           : QuickCropOpts{QuickCropOpts::Mode::None, false, QString()};
      openImageSource(opts.src, opts.frame);
    } else if (!opts.file.isEmpty()) {
      pendingLaunchLayout_ = opts.layout;
      openPathFromOS(opts.file, opts.frame);
    }

    // --projects: open the Projects window at launch. Queued so it runs after the
    // current call unwinds (and after a primary load has been kicked off).
    if (opts.projects) QTimer::singleShot(0, this, &MainWindow::openProjects);
  }

  // A stencil:// deep link arriving on a RUNNING app (macOS QFileOpenEvent url).
  // Same fields as a launch, minus the theme/projects extras.
  void MainWindow::openStencilUrl(const QUrl& url) {
    const LaunchOptions opts = parseStencilUrl(url);
    if (opts.empty()) {
      notify_->error("Could not read the stencil:// link");
      return;
    }
    applyLaunchOptions(opts);
  }

  // Deep-link server open: connect like a fresh manual client, then open the project.
  void MainWindow::openServerLaunch(const QString& serverUrl, const QString& id,
                                    bool incognito) {
    // normalizeBase throws no exceptions but yields "" on junk — guard it.
    const QString url = stencil::net::ServerClient::normalizeBase(serverUrl);
    if (url.isEmpty()) {
      notify_->error("Bad server URL in the link");
      return;
    }
    if (incognito && actIncognito_->isEnabled()) actIncognito_->setChecked(true);
    auto* mgr = ensureConnections();
    if (!mgr->find(url)) {
      // Reuse the saved token for this origin (the browser's saved-servers parity);
      // else connect tokenless and the server mints one (POST /auth/token).
      QString token;
      auto kind = stencil::net::ServerClient::CredentialKind::None;
      bool known = false;
      for (const auto& s : stencil::net::connectionStore::loadSavedServers()) {
        if (stencil::net::ServerClient::normalizeBase(s.url) == url) {
          token = s.token;
          kind = stencil::net::ServerClient::kindFromTag(s.kind);
          known = true;
          break;
        }
      }
      // A deep link can name ANY server — don't let a drive-by stencil:// URL
      // silently add a (persisted) connection to an origin this machine has never
      // used. Known origins (live or saved) skip the prompt.
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
          // The normal connect path: surface the failure and open the Servers dialog
          // so the user can supply a token / fix the URL.
          notify_->error(QString("Could not connect to %1 — %2").arg(url, err));
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
