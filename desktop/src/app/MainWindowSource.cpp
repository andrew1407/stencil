#include "MainWindow.hpp"
#include "Notifications.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "ServerClient.hpp"
#include "theme.hpp"
#include "../support/DisintegrateOverlay.hpp"

#include <QFileInfo>
#include <QImage>

// Loading a source by path or URL, and the .stencil project file.

namespace stencil::gui {

  void MainWindow::ensureMediaLoader() {
    if (mediaLoader) return;
    mediaLoader = new MediaLoader(this);
    connect(mediaLoader, &MediaLoader::loaded, this,
            &MainWindow::onLaunchImageLoaded);
    connect(mediaLoader, &MediaLoader::failed, this, [this](const QString& msg) {
      pendingLaunchLayout.clear();
      pendingLaunchLayoutJson.clear();
      pendingProvSource.clear();
      pendingProvResource.clear();
      pendingServerTarget.clear();
      notify->error(msg);
    });
  }

  void MainWindow::openImageSource(const QString& src, int frame) {
    // A data: URL (browser→desktop stencil:// hand-off) decodes directly; MediaLoader has no data
    // URI path.
    if (src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
      const int comma = src.indexOf(QLatin1Char(','));
      QImage img;
      bool ok = comma > 0;
      if (ok) {
        const QString meta = src.left(comma);
        const QByteArray payload = src.mid(comma + 1).toUtf8();
        const QByteArray bytes = meta.contains(QLatin1String(";base64"), Qt::CaseInsensitive)
                                     ? QByteArray::fromBase64(payload)
                                     : QByteArray::fromPercentEncoding(payload);
        ok = img.loadFromData(bytes);
      }
      if (!ok) {
        pendingLaunchLayout.clear();
        pendingLaunchLayoutJson.clear();
        notify->error("Could not decode the inline image");
        return;
      }
      onLaunchImageLoaded(img, QString());
      return;
    }
    ensureMediaLoader();
    mediaLoader->load(src, frame);
  }

  // From the OS shell: a *.json is a layout, anything else goes via the --src path.
  void MainWindow::openPathFromOS(const QString& path, int frame) {
    if (path.isEmpty()) return;
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare("json", Qt::CaseInsensitive) == 0) {
      applyLayoutFromSource(path);
      return;
    }
    if (suffix.compare("stencil", Qt::CaseInsensitive) == 0) {
      openProjectFile(path);
      return;
    }
    if (suffix.compare("stc", Qt::CaseInsensitive) == 0) {
      runScriptFromFile(path);
      return;
    }
    openImageSource(path, frame);
  }

  // Open a portable .stencil project; mirrors browser DrawingApp.applyProjectFile.
  void MainWindow::openProjectFile(const QString& path) {
    QByteArray bytes;
    if (!readFileBytes(path, bytes)) {
      notify->error("Could not read the project file");
      return;
    }
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(bytes, pf, &err)) {
      notify->error("Invalid .stencil file: " + err);
      return;
    }
    QImage img;
    if (!img.loadFromData(pf.imageBytes)) {
      notify->error("Could not decode the project image");
      return;
    }
    activeProjectId.clear();   // an opened project file is a fresh editor (Save to Project keeps it)
    loadImageWithLayout(img, pf.layout, pf.imageBytes, pf.imageExt);
    currentSource = pf.source;
    currentResource = pf.resource;
    // Only a file that carried a theme changes the user's; a custom-hex accent is ignored (desktop
    // uses presets).
    if (pf.hasTheme) {
      bool changed = false;
      if (pf.themeMode == "light" || pf.themeMode == "dark") {
        settings.themeMode = pf.themeMode;
        changed = true;
      }
      if (!pf.themeAccent.isEmpty()) {
        for (const auto& preset : accentPresets()) {
          if (pf.themeAccent == preset.key) {
            settings.accentColor = pf.themeAccent;
            changed = true;
            break;
          }
        }
      }
      if (changed) {
        applyTheme();
        fileStore::saveSettings(settings);
      }
    }
    createLocalProject(pf.name, /*announce=*/false, /*fromFile=*/true);
    linkStencilFile(path, bytes);
    // Chat persistence (§12.3): adopt the file's saved chat when the opt-in is on.
    if (settings.saveChatsWithProject) {
      restoreChatFromDoc(pf.chat);
      if (!pf.chat.isEmpty()) {
        if (Project* pr = findProject(activeProjectId.toStdString())) {
          pr->chat = buildActiveChatDoc();
          fileStore::saveProjects(projectList);
        }
      }
    }
    fitToWindow();
    playImageArrival();   // a .stencil open is a fresh image landing (browser: ghostIn)
  }

  // .stencil bytes (original image + layout + metadata + theme); mirrors browser
  // ExportService.saveProjectFile.
  void MainWindow::setSourceBytes(const QByteArray& bytes, const QString& ext) {
    sourceBytes = bytes;
    sourceExt = ext.trimmed().toLower();
  }

  // Retained raw bytes so a .stencil bundle embeds the untouched original; cleared on a read
  // failure.
  void MainWindow::retainSourceFromFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    QByteArray bytes;
    if (!ext.isEmpty()) readFileBytes(path, bytes);
    setSourceBytes(bytes, ext);   // empty bytes ⇒ buildStencilBytes re-encodes from pixels
  }

  bool MainWindow::openProjectByName(const QString& name) {
    const QString want = name.trimmed();
    for (const auto& p : projectList) {
      if (QString::fromStdString(p.meta.name).compare(want, Qt::CaseInsensitive) ==
          0)
        return loadProjectIntoCanvas(QString::fromStdString(p.meta.id));
    }
    return false;
  }

}  // namespace stencil::gui
