#include "MainWindow.hpp"
#include "ProjectFlows.hpp"
#include "deepLink.hpp"
#include "displayName.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "modalReveal.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "../../../support/rowWork.hpp"

#include <QBuffer>
#include <QDesktopServices>

// "Open in another app": the source pick and the dispatch to browser/CLI/file manager.

namespace stencil::gui {

  // "Open in…": a server-linked session sends only the server reference (no token in any link); a local session embeds
  // image + layout in the browser fragment. Telegram is server-projects-only (a 64-char start payload).
  void ProjectFlows::openInAnotherApp() {
    if (!w.canvas->hasImage()) {
      w.notify->error("Load an image first");
      return;
    }
    const bool serverProject = !w.remote.session->getLink().address.isEmpty() && !w.remote.session->getLink().id.isEmpty();
    const QString botUsername = w.settings.telegramBotUsername.trimmed();
    const bool browserAvailable = !w.settings.browserBaseUrl.trimmed().isEmpty();
    const bool telegramAvailable = !botUsername.isEmpty() && serverProject;
    if (!browserAvailable && !telegramAvailable) {
      // Guard (acts.openIn is hidden when nothing's available).
      w.notify->info("Nothing to open into — set a browser URL in Settings "
                    "(or a Telegram bot for server projects).");
      return;
    }
    OpenInSource src;
    src.serverUrl = w.remote.session->getLink().address;
    src.serverId = w.remote.session->getLink().id;
    src.version = w.remote.session->getLink().version;
    if (!serverProject) {
      src.image = w.canvas->getOriginalImage();
      src.name = w.projectBaseName();
      src.source = w.docSource.currentSource;
      src.resource = w.docSource.currentResource;
      src.layout = fileStore::buildLayoutJson(
          w.canvas->imageWidth(), w.canvas->imageHeight(), w.canvas->allLines(),
          w.settings.imageFilter, w.settings.filterColor, w.canvas->getCropRect(),
          w.canvas->getRotationQuarters(), w.currentLayoutMeta());
    }
    src.startIncognito = w.incognito;
    dispatchOpenIn(src, browserAvailable, telegramAvailable,
                   [this](OpenInDialog& dlg) { return w.execMaybePopover(dlg); });
  }

  // The hand-off itself; `run` shows the dialog as a popover off the toolbar, or out of a projects row.
  void ProjectFlows::dispatchOpenIn(const OpenInSource& src, bool browserAvailable,
                                  bool telegramAvailable,
                                  const std::function<int(OpenInDialog&)>& run) {
    const QString botUsername = w.settings.telegramBotUsername.trimmed();
    const bool serverProject = !src.serverUrl.isEmpty() && !src.serverId.isEmpty();
    OpenInDialog dlg(&w, serverProject, src.serverUrl, browserAvailable, telegramAvailable,
                     src.startIncognito, src.serverId);
    // The 64-char overflow stays IN the dialog (the browser's fallback row) while the bot chat opens alongside.
    QObject::connect(&dlg, &OpenInDialog::telegramFallback, &w, [botUsername] {
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://t.me/") + botUsername));
    });
    QObject::connect(&dlg, &OpenInDialog::toast, &w,
                     [this](const QString& text, bool fail) {
                       if (fail) w.notify->error(text);
                       else w.notify->success(text);
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
        deepLink::buildBrowserLaunchUrl(w.settings.browserBaseUrl, payload);
    // The OS launcher's argv tolerates far less than an in-page URL: refuse absurd payloads, warn on large ones.
    if (!serverProject && url.size() > 1000000) {
      w.notify->error(
          "Image too large to hand off inline — save it to a server and share the server link");
      return;
    }
    if (!serverProject && url.size() > 200000)
      w.notify->info("Large image — the hand-off may fail; prefer saving to a server");
    QDesktopServices::openUrl(QUrl(url));
  }

  // The projects list's per-row hand-off (browser projectsModal.js); the session's filter and page ride along since a project does not persist them.
  void ProjectFlows::openInAnotherAppFor(const QString& id, const QString& serverUrl,
                                      const QRect& closeRect) {
    const bool browserAvailable = !w.settings.browserBaseUrl.trimmed().isEmpty();
    const bool serverProject = !serverUrl.isEmpty();
    const bool telegramAvailable = !w.settings.telegramBotUsername.trimmed().isEmpty() && serverProject;
    if (!browserAvailable && !telegramAvailable) {
      w.notify->info("Nothing to open into — set a browser URL in Settings "
                    "(or a Telegram bot for server projects).");
      return;
    }
    if (!serverProject && id == w.activeProjectId && w.canvas->hasImage()) {
      openInAnotherApp();
      return;
    }

    OpenInSource src;
    src.serverUrl = serverUrl;
    src.serverId = id;
    // Read now: a local project's picture decodes on the pool first, and the pointer moves on.
    const QRect from = support::gestureAnchorRect();
    const auto dispatch = [this, browserAvailable, telegramAvailable, from, closeRect](const OpenInSource& ready) {
      dispatchOpenIn(ready, browserAvailable, telegramAvailable, [from, closeRect](OpenInDialog& dlg) {
        support::revealDialog(dlg, nullptr, from, closeRect);
        return dlg.exec();
      });
    };
    if (serverProject) return dispatch(src);
    Project* pr = w.findProject(id.toStdString());
    if (!pr) { w.notify->error("That project could not be read"); return; }
    src.name = support::shortName(QString::fromStdString(pr->meta.name));
    src.source = QString::fromStdString(pr->meta.source);
    src.resource = QString::fromStdString(pr->meta.resource);
    const auto withImage = [this, id, dispatch](OpenInSource ready) {
      const Project* now = w.findProject(id.toStdString());
      if (!now) { w.notify->error("That project could not be read"); return; }
      if (ready.image.isNull()) { w.notify->error("That project's image could not be loaded"); return; }
      ready.layout = fileStore::buildLayoutJson(ready.image.width(), ready.image.height(), now->lines,
                                                w.settings.imageFilter, w.settings.filterColor,
                                                now->cropRect, now->rotationQuarters, w.currentLayoutMeta());
      dispatch(ready);
    };
    // The ORIGINAL bytes: lines, crop and rotation travel in the layout. A blank project has no file — it is its fill.
    if (pr->imagePath.isEmpty()) {
      if (pr->meta.blank && pr->meta.imageW > 0 && pr->meta.imageH > 0) {
        src.image = QImage(pr->meta.imageW, pr->meta.imageH, QImage::Format_ARGB32);
        const QColor fill(QString::fromStdString(pr->meta.blankColor));
        src.image.fill(fill.isValid() ? fill : QColor(Qt::white));
      }
      return withImage(src);
    }
    const QString path = pr->imagePath;
    support::runOnPool<QImage>(&w, [path] { return QImage(path); }, [src, withImage](QImage img) {
      OpenInSource ready = src;
      ready.image = img;
      withImage(ready);
    });
  }

}  // namespace stencil::gui
