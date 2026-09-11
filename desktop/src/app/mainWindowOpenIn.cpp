#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "deepLink.hpp"
#include "displayName.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "overlayScrollArea.hpp"
#include "cropDialog.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "remoteSession.hpp"
#include "../support/modalChrome.hpp"

#include <QBuffer>
#include <QDesktopServices>
#include <QImage>
#include <QJsonObject>
#include <QUrl>

// "Open in another app": the source pick and the dispatch to browser/CLI/file manager.

namespace stencil::gui {

  // "Open in…" — mirror the current session into the browser app or the Telegram
  // bot. A server-linked session sends only the server reference (the receiver
  // connects like a fresh client — no token in any link); a local/incognito session
  // embeds the image + full layout inline in the browser fragment. Telegram is
  // server-projects-only (image bytes can't ride a 64-char start payload).
  void MainWindow::openInAnotherApp() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    const bool serverProject = !remoteSession_->link().address.isEmpty() && !remoteSession_->link().id.isEmpty();
    const QString botUsername = settings_.telegramBotUsername.trimmed();
    const bool browserAvailable = !settings_.browserBaseUrl.trimmed().isEmpty();
    const bool telegramAvailable = !botUsername.isEmpty() && serverProject;
    if (!browserAvailable && !telegramAvailable) {
      // Shouldn't happen (actOpenIn_ is hidden when nothing's available), but guard.
      notify_->info("Nothing to open into — set a browser URL in Settings "
                    "(or a Telegram bot for server projects).");
      return;
    }
    OpenInSource src;
    src.serverUrl = remoteSession_->link().address;
    src.serverId = remoteSession_->link().id;
    src.version = remoteSession_->link().version;
    if (!serverProject) {
      src.image = canvas_->originalImage();
      src.name = projectBaseName();
      src.source = currentSource_;
      src.resource = currentResource_;
      src.layout = fileStore::buildLayoutJson(
          canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
          settings_.imageFilter, settings_.filterColor, canvas_->cropRect(),
          canvas_->rotationQuarters(), currentLayoutMeta());
    }
    src.startIncognito = incognito_;
    dispatchOpenIn(src, browserAvailable, telegramAvailable,
                   [this](OpenInDialog& dlg) { return execMaybePopover(dlg); });
  }

  // The hand-off itself, whichever side gathered the material: the dialog, the Telegram
  // branch, the #stencil= payload and the size gates. `run` shows the dialog — the live
  // session opens it as a popover off the toolbar, a projects row flies it out of the row.
  void MainWindow::dispatchOpenIn(const OpenInSource& src, bool browserAvailable,
                                  bool telegramAvailable,
                                  const std::function<int(OpenInDialog&)>& run) {
    const QString botUsername = settings_.telegramBotUsername.trimmed();
    const bool serverProject = !src.serverUrl.isEmpty() && !src.serverId.isEmpty();
    OpenInDialog dlg(this, serverProject, src.serverUrl, browserAvailable, telegramAvailable,
                     src.startIncognito, src.serverId);
    // The 64-char overflow (very long host) stays IN the dialog — the browser's
    // fallback row with the two bot commands — while the bot chat opens alongside.
    connect(&dlg, &OpenInDialog::telegramFallback, this, [botUsername] {
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://t.me/") + botUsername));
    });
    connect(&dlg, &OpenInDialog::toast, this,
            [this](const QString& text, bool fail) {
              if (fail) notify_->error(text);
              else notify_->success(text);
            });
    if (run(dlg) != QDialog::Accepted) return;
    const bool incog = dlg.incognito();

    if (dlg.outcome() == OpenInDialog::Outcome::Telegram) {
      if (!serverProject) return;  // the dialog disables this outcome anyway
      const QString payload =
          deepLink::encodeTelegramStartPayload(src.serverUrl, src.serverId);
      if (payload.isEmpty()) return;   // the dialog only accepts once the link fits
      QDesktopServices::openUrl(QUrl(deepLink::buildTelegramLink(botUsername, payload)));
      return;
    }

    // Browser app.
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
        deepLink::buildBrowserLaunchUrl(settings_.browserBaseUrl, payload);
    // Inline hand-offs ride the OS launcher's argv, which tolerates far less than an
    // in-page URL: refuse absurd payloads, warn on large ones (server links stay tiny).
    if (!serverProject && url.size() > 1000000) {
      notify_->error(
          "Image too large to hand off inline — save it to a server and share the server link");
      return;
    }
    if (!serverProject && url.size() > 200000)
      notify_->info("Large image — the hand-off may fail; prefer saving to a server");
    QDesktopServices::openUrl(QUrl(url));
  }

  // The projects list's per-row hand-off (browser parity: projectsModal.js). The open
  // project keeps the live path, unsaved edits and all; any other row is read from what is
  // saved. The session's filter and page settings ride along, because a project does not
  // persist its own — the same approximation made when reopening it.
  void MainWindow::openInAnotherAppFor(const QString& id, const QString& serverUrl,
                                      const QRect& closeRect) {
    const bool browserAvailable = !settings_.browserBaseUrl.trimmed().isEmpty();
    const bool serverProject = !serverUrl.isEmpty();
    const bool telegramAvailable = !settings_.telegramBotUsername.trimmed().isEmpty() && serverProject;
    if (!browserAvailable && !telegramAvailable) {
      notify_->info("Nothing to open into — set a browser URL in Settings "
                    "(or a Telegram bot for server projects).");
      return;
    }
    // The open project: hand over what is on the canvas, unsaved edits and all.
    if (!serverProject && id == activeProjectId_ && canvas_->hasImage()) {
      openInAnotherApp();
      return;
    }

    OpenInSource src;
    src.serverUrl = serverUrl;
    src.serverId = id;
    if (!serverProject) {
      Project* pr = findProject(id.toStdString());
      if (!pr) { notify_->error("That project could not be read"); return; }
      src.name = support::shortName(QString::fromStdString(pr->meta.name));
      src.source = QString::fromStdString(pr->meta.source);
      src.resource = QString::fromStdString(pr->meta.resource);
      // The ORIGINAL bytes: lines, crop and rotation travel in the layout, exactly as the
      // live path sends canvas_->originalImage(). A blank project has no file — it is its
      // fill (buildProjectThumbs restores the same way).
      if (!pr->imagePath.isEmpty()) {
        src.image = QImage(pr->imagePath);
      } else if (pr->meta.blank && pr->meta.imageW > 0 && pr->meta.imageH > 0) {
        src.image = QImage(pr->meta.imageW, pr->meta.imageH, QImage::Format_ARGB32);
        const QColor fill(QString::fromStdString(pr->meta.blankColor));
        src.image.fill(fill.isValid() ? fill : QColor(Qt::white));
      }
      if (src.image.isNull()) { notify_->error("That project's image could not be loaded"); return; }
      src.layout = fileStore::buildLayoutJson(src.image.width(), src.image.height(), pr->lines,
                                              settings_.imageFilter, settings_.filterColor,
                                              pr->cropRect, pr->rotationQuarters, currentLayoutMeta());
    }
    dispatchOpenIn(src, browserAvailable, telegramAvailable, [&](OpenInDialog& dlg) {
      support::revealDialog(dlg, nullptr, support::gestureAnchorRect(), closeRect);
      return dlg.exec();
    });
  }

}  // namespace stencil::gui
