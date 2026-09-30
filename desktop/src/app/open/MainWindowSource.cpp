#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "../../support/rowWork.hpp"
#include "launchOptions.hpp"

#include <QFileInfo>
#include <QImage>
#include <QTimer>
#include <QUrl>

#include <optional>

// What the OS hands the window — the launch options, a stencil:// link, a path routed by suffix —
// and the pool decode every picture load goes through.

namespace stencil::gui {

  // From the OS shell: a *.json is a layout, anything else goes via the --src path.
  void MainWindow::openPathFromOS(const QString& path, int frame) {
    if (path.isEmpty()) return;
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare("json", Qt::CaseInsensitive) == 0) {
      parts.sourceOpener.applyLayoutFromSource(path);
      return;
    }
    if (suffix.compare("stencil", Qt::CaseInsensitive) == 0) {
      parts.sourceOpener.openProjectFile(path);
      return;
    }
    if (suffix.compare("stc", Qt::CaseInsensitive) == 0) {
      parts.scriptHost.runScriptFromFile(path);
      return;
    }
    parts.sourceOpener.openImageSource(path, frame);
  }

  void MainWindow::decodeForCanvas(std::function<QImage()> decode,
                                   std::function<void(const QImage&)> adopt,
                                   std::function<void()> dropped) {
    const quint64 load = canvas->beginPictureLoad();
    auto landed = [this, load, adopt = std::move(adopt), dropped = std::move(dropped)](QImage img) {
      if (canvas->pictureGeneration() != load) {
        if (dropped) dropped();
        return;
      }
      adopt(img);
    };
    support::runOnPool<QImage>(this, std::move(decode), std::move(landed));
  }

  // The desktop twin of the browser's URL launch (applyExternalLaunch + applyProjectDeepLink). Runs after show(): resolution is async.
  void MainWindow::applyLaunchOptions(const LaunchOptions& opts) {
    if (opts.empty()) return;

    // Incognito set FIRST so it gates the theme persist and every later write.
    if (opts.incognito && opts.project.isEmpty() && acts.incognito->isEnabled())
      acts.incognito->setChecked(true);  // drives incognito via its toggled slot

    if (opts.hasTheme) {
      settings.themeMode = (opts.theme == "dark") ? "dark" : "light";
      applySettings(settings, /*persist=*/true);
    }

    // Taken before any load starts, so a linked script can tell when that picture has landed.
    const qint64 pictureBefore = canvas->getOriginalImage().cacheKey();
    const bool picture = !opts.project.isEmpty() || !opts.serverProjectId.isEmpty() || !opts.src.isEmpty() ||
                         !opts.file.isEmpty();

    // Priority: --project > stencil:// server reference > --src > positional file.
    if (!opts.project.isEmpty()) {
      if (!parts.projects.openProjectByName(opts.project))
        notify->error(QString("No project named \"%1\"").arg(opts.project));
    } else if (!opts.serverUrl.isEmpty() && !opts.serverProjectId.isEmpty()) {
      // Queued so the connect + download run after show().
      const QString url = opts.serverUrl, id = opts.serverProjectId;
      const bool incog = opts.incognito;
      QTimer::singleShot(0, this, [this, url, id, incog] {
        parts.projects.openServerLaunch(url, id, incog);
      });
    } else if (!opts.src.isEmpty()) {
      docSource.pendingLaunchLayout = opts.layout;  // applied after the image loads
      docSource.pendingLaunchLayoutJson = opts.layoutJson;
      // Quick-crop override from the "Open in new window" handoff; consumed by applyQuickCrop().
      if (opts.hasCropOverride)
        docSource.pendingCrop =
            opts.cropToPage
                ? QuickCropOpts{QuickCropOpts::Mode::PAGE, opts.cropAlbum, opts.cropPage,
                                {opts.cropX, opts.cropY, opts.cropW, opts.cropH}}
                : QuickCropOpts::none();
      parts.sourceOpener.openImageSource(opts.src, opts.frame, opts.srcFallbacks);
    } else if (!opts.file.isEmpty()) {
      docSource.pendingLaunchLayout = opts.layout;
      openPathFromOS(opts.file, opts.frame);
    }

    // Queued so it runs after a primary load has been kicked off.
    if (opts.projects) QTimer::singleShot(0, this, [this] { parts.projects.openProjects(); });

    if (opts.scriptDropped)
      notify->error(QString("The script in the link was longer than %1 characters, so it was left out")
                        .arg(launchScriptMaxChars()));
    if (!opts.script.isEmpty())
      parts.scriptHost.adoptLinkedScript(opts.script, opts.scriptMode == QLatin1String("open"),
                                         picture ? std::optional<qint64>(pictureBefore) : std::nullopt);
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

}  // namespace stencil::gui
