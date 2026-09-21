#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "deepLink.hpp"
#include "displayName.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "OverlayScrollArea.hpp"
#include "CropDialog.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "launchOptions.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QBuffer>
#include <QDesktopServices>
#include <QImage>
#include <QJsonObject>
#include <QUrl>

// "Open in another app": the source pick and the dispatch to browser/CLI/file manager.

namespace stencil::gui {

  // "Open in…": a server-linked session sends only the server reference (no token in any link); a local session embeds
  // image + layout in the browser fragment. Telegram is server-projects-only (a 64-char start payload).
  void MainWindow::openInAnotherApp() {
    if (!canvas->hasImage()) {
      notify->error("Load an image first");
      return;
    }
    const bool serverProject = !remoteSession->getLink().address.isEmpty() && !remoteSession->getLink().id.isEmpty();
    const QString botUsername = settings.telegramBotUsername.trimmed();
    const bool browserAvailable = !settings.browserBaseUrl.trimmed().isEmpty();
    const bool telegramAvailable = !botUsername.isEmpty() && serverProject;
    if (!browserAvailable && !telegramAvailable) {
      // Guard (actOpenIn is hidden when nothing's available).
      notify->info("Nothing to open into — set a browser URL in Settings "
                    "(or a Telegram bot for server projects).");
      return;
    }
    OpenInSource src;
    src.serverUrl = remoteSession->getLink().address;
    src.serverId = remoteSession->getLink().id;
    src.version = remoteSession->getLink().version;
    if (!serverProject) {
      src.image = canvas->getOriginalImage();
      src.name = projectBaseName();
      src.source = currentSource;
      src.resource = currentResource;
      src.layout = fileStore::buildLayoutJson(
          canvas->imageWidth(), canvas->imageHeight(), canvas->allLines(),
          settings.imageFilter, settings.filterColor, canvas->getCropRect(),
          canvas->getRotationQuarters(), currentLayoutMeta());
    }
    src.startIncognito = incognito;
    dispatchOpenIn(src, browserAvailable, telegramAvailable,
                   [this](OpenInDialog& dlg) { return execMaybePopover(dlg); });
  }

  // The hand-off itself; `run` shows the dialog as a popover off the toolbar, or out of a projects row.
  void MainWindow::dispatchOpenIn(const OpenInSource& src, bool browserAvailable,
                                  bool telegramAvailable,
                                  const std::function<int(OpenInDialog&)>& run) {
    const QString botUsername = settings.telegramBotUsername.trimmed();
    const bool serverProject = !src.serverUrl.isEmpty() && !src.serverId.isEmpty();
    OpenInDialog dlg(this, serverProject, src.serverUrl, browserAvailable, telegramAvailable,
                     src.startIncognito, src.serverId);
    // The 64-char overflow stays IN the dialog (the browser's fallback row) while the bot chat opens alongside.
    connect(&dlg, &OpenInDialog::telegramFallback, this, [botUsername] {
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://t.me/") + botUsername));
    });
    connect(&dlg, &OpenInDialog::toast, this,
            [this](const QString& text, bool fail) {
              if (fail) notify->error(text);
              else notify->success(text);
            });
    if (run(dlg) != QDialog::Accepted) return;
    const bool incog = dlg.getIncognito();

    if (dlg.getOutcome() == OpenInDialog::Outcome::TELEGRAM) {
      if (!serverProject) return;  // the dialog disables this outcome anyway
      const QString payload =
          deepLink::encodeTelegramStartPayload(src.serverUrl, src.serverId);
      if (payload.isEmpty()) return;   // the dialog only accepts once the link fits
      QDesktopServices::openUrl(QUrl(deepLink::buildTelegramLink(botUsername, payload)));
      return;
    }

    QJsonObject payload;
    if (serverProject) {
      QJsonObject server;
      server["url"] = src.serverUrl;
      server["id"] = src.serverId;
      if (src.version > 0) server["version"] = src.version;
      payload["server"] = server;
    } else {
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      src.image.save(&buf, "PNG");
      payload["dataUrl"] =
          QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
      payload["name"] = src.name + ".png";
      payload["layout"] = src.layout;
      if (!src.source.isEmpty()) payload["source"] = src.source;
      if (!src.resource.isEmpty()) payload["resource"] = src.resource;
    }
    if (incog) payload["incognito"] = true;

    const QString url =
        deepLink::buildBrowserLaunchUrl(settings.browserBaseUrl, payload);
    // The OS launcher's argv tolerates far less than an in-page URL: refuse absurd payloads, warn on large ones.
    if (!serverProject && url.size() > 1000000) {
      notify->error(
          "Image too large to hand off inline — save it to a server and share the server link");
      return;
    }
    if (!serverProject && url.size() > 200000)
      notify->info("Large image — the hand-off may fail; prefer saving to a server");
    QDesktopServices::openUrl(QUrl(url));
  }

  // The projects list's per-row hand-off (browser projectsModal.js); the session's filter and page ride along since a project does not persist them.
  void MainWindow::openInAnotherAppFor(const QString& id, const QString& serverUrl,
                                      const QRect& closeRect) {
    const bool browserAvailable = !settings.browserBaseUrl.trimmed().isEmpty();
    const bool serverProject = !serverUrl.isEmpty();
    const bool telegramAvailable = !settings.telegramBotUsername.trimmed().isEmpty() && serverProject;
    if (!browserAvailable && !telegramAvailable) {
      notify->info("Nothing to open into — set a browser URL in Settings "
                    "(or a Telegram bot for server projects).");
      return;
    }
    if (!serverProject && id == activeProjectId && canvas->hasImage()) {
      openInAnotherApp();
      return;
    }

    OpenInSource src;
    src.serverUrl = serverUrl;
    src.serverId = id;
    if (!serverProject) {
      Project* pr = findProject(id.toStdString());
      if (!pr) { notify->error("That project could not be read"); return; }
      src.name = support::shortName(QString::fromStdString(pr->meta.name));
      src.source = QString::fromStdString(pr->meta.source);
      src.resource = QString::fromStdString(pr->meta.resource);
      // The ORIGINAL bytes: lines, crop and rotation travel in the layout. A blank project has no file — it is its fill.
      if (!pr->imagePath.isEmpty()) {
        src.image = QImage(pr->imagePath);
      } else if (pr->meta.blank && pr->meta.imageW > 0 && pr->meta.imageH > 0) {
        src.image = QImage(pr->meta.imageW, pr->meta.imageH, QImage::Format_ARGB32);
        const QColor fill(QString::fromStdString(pr->meta.blankColor));
        src.image.fill(fill.isValid() ? fill : QColor(Qt::white));
      }
      if (src.image.isNull()) { notify->error("That project's image could not be loaded"); return; }
      src.layout = fileStore::buildLayoutJson(src.image.width(), src.image.height(), pr->lines,
                                              settings.imageFilter, settings.filterColor,
                                              pr->cropRect, pr->rotationQuarters, currentLayoutMeta());
    }
    dispatchOpenIn(src, browserAvailable, telegramAvailable, [&](OpenInDialog& dlg) {
      support::revealDialog(dlg, nullptr, support::gestureAnchorRect(), closeRect);
      return dlg.exec();
    });
  }

}  // namespace stencil::gui
