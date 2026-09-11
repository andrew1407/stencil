#include "mainWindow.hpp"
#include "notifications.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "serverClient.hpp"
#include "theme.hpp"
#include "../support/disintegrateOverlay.hpp"

#include <QFileInfo>
#include <QImage>

// Loading a source by path or URL, and the .stencil project file.

namespace stencil::gui {

  // Lazily construct + wire the async --src resolver (image / URL / video frame).
  void MainWindow::ensureMediaLoader() {
    if (mediaLoader_) return;
    mediaLoader_ = new MediaLoader(this);
    connect(mediaLoader_, &MediaLoader::loaded, this,
            &MainWindow::onLaunchImageLoaded);
    connect(mediaLoader_, &MediaLoader::failed, this, [this](const QString& msg) {
      pendingLaunchLayout_.clear();
      pendingLaunchLayoutJson_.clear();
      pendingProvSource_.clear();
      pendingProvResource_.clear();
      pendingServerTarget_.clear();
      notify_->error(msg);
    });
  }

  void MainWindow::openImageSource(const QString& src, int frame) {
    // Inline data: URL (a browser→desktop stencil:// hand-off): decode directly —
    // MediaLoader resolves paths/URLs/video, not data URIs.
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
        pendingLaunchLayout_.clear();
        pendingLaunchLayoutJson_.clear();
        notify_->error("Could not decode the inline image");
        return;
      }
      onLaunchImageLoaded(img, QString());
      return;
    }
    ensureMediaLoader();
    mediaLoader_->load(src, frame);
  }

  // Open a file handed in by the OS shell (file-association / "Open With" / drop):
  // a *.json is a layout (applied onto the current image), anything else is an
  // image or video opened via the --src path.
  void MainWindow::openPathFromOS(const QString& path, int frame) {
    if (path.isEmpty()) return;
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare("json", Qt::CaseInsensitive) == 0) {
      applyLayoutFromSource(path);
      return;
    }
    // A whole .stencil project (double-click / drag / file arg) loads image + layout + theme.
    if (suffix.compare("stencil", Qt::CaseInsensitive) == 0) {
      openProjectFile(path);
      return;
    }
    openImageSource(path, frame);
  }

  // Open a portable .stencil project: decode its embedded ORIGINAL image, adopt its layout, provenance, and (only if present) theme. Mirrors browser DrawingApp.applyProjectFile.
  void MainWindow::openProjectFile(const QString& path) {
    QByteArray bytes;
    if (!readFileBytes(path, bytes)) {
      notify_->error("Could not read the project file");
      return;
    }
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(bytes, pf, &err)) {
      notify_->error("Invalid .stencil file: " + err);
      return;
    }
    QImage img;
    if (!img.loadFromData(pf.imageBytes)) {
      notify_->error("Could not decode the project image");
      return;
    }
    activeProjectId_.clear();   // an opened project file is a fresh editor (Save to Project keeps it)
    loadImageWithLayout(img, pf.layout, pf.imageBytes, pf.imageExt);
    currentSource_ = pf.source;
    currentResource_ = pf.resource;
    // Apply the file's theme only when it carried one, so opening a themeless project never
    // changes the user's current theme. A custom-hex accent is ignored (desktop uses presets).
    if (pf.hasTheme) {
      bool changed = false;
      if (pf.themeMode == "light" || pf.themeMode == "dark") {
        settings_.themeMode = pf.themeMode;
        changed = true;
      }
      if (!pf.themeAccent.isEmpty()) {
        for (const auto& preset : accentPresets()) {
          if (pf.themeAccent == preset.key) {
            settings_.accentColor = pf.themeAccent;
            changed = true;
            break;
          }
        }
      }
      if (changed) {
        applyTheme();
        fileStore::saveSettings(settings_);
      }
    }
    // Persist as a local file-origin project so it shows the bronze .stencil outline + badge in the Projects list.
    createLocalProject(pf.name, /*announce=*/false, /*fromFile=*/true);
    // Link this file as the project's live-sync target (auto-save + watch when live sync is on).
    linkStencilFile(path, bytes);
    // Chat persistence (§12.3): adopt the file's saved chat when the opt-in is
    // on (an absent one = a fresh scope), and carry it onto the new local record.
    if (settings_.saveChatsWithProject) {
      restoreChatFromDoc(pf.chat);
      if (!pf.chat.isEmpty()) {
        if (Project* pr = findProject(activeProjectId_.toStdString())) {
          pr->chat = buildActiveChatDoc();
          fileStore::saveProjects(projectList_);
        }
      }
    }
    fitToWindow();
    playImageArrival();   // a .stencil open is a fresh image landing (browser: ghostIn)
  }

  // Serialize the current project to .stencil bytes (ORIGINAL image + layout + metadata + theme); shared by Save Project As and live-sync auto-save. Mirrors browser ExportService.saveProjectFile.
  void MainWindow::setSourceBytes(const QByteArray& bytes, const QString& ext) {
    sourceBytes_ = bytes;
    sourceExt_ = ext.trimmed().toLower();
  }

  // Read + retain a local image file's raw bytes so a later .stencil bundle embeds the untouched
  // original (lossless). Clears the retained source on a read failure or a missing suffix.
  void MainWindow::retainSourceFromFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    QByteArray bytes;
    if (!ext.isEmpty()) readFileBytes(path, bytes);
    setSourceBytes(bytes, ext);   // empty bytes ⇒ buildStencilBytes re-encodes from pixels
  }

  bool MainWindow::openProjectByName(const QString& name) {
    const QString want = name.trimmed();
    for (const auto& p : projectList_) {
      if (QString::fromStdString(p.meta.name).compare(want, Qt::CaseInsensitive) ==
          0)
        return loadProjectIntoCanvas(QString::fromStdString(p.meta.id));
    }
    return false;
  }

}  // namespace stencil::gui
