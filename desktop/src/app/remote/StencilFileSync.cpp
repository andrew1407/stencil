#include "MainWindow.hpp"
#include "stencilFileSyncParts.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"  // showSaveDialog
#include "../../support/modal/modalChrome.hpp"  // confirmModalChoice — the browser-styled question
#include "iconSet.hpp"
#include "Notifications.hpp"
#include "theme.hpp"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSet>
#include <QTimer>

// The .stencil project-file sync block: serialize, Save-As / delete, the file link + watcher,
// auto-save and merge.

namespace stencil::gui {

  QByteArray MainWindow::buildStencilBytes() {
    if (!canvas->hasImage()) return {};
    const QImage& orig = canvas->getOriginalImage();
    QByteArray png;
    QString ext = QStringLiteral("png");
    if (!sourceBytes.isEmpty()) {
      png = sourceBytes;                                  // untouched original (lossless)
      if (!sourceExt.isEmpty()) ext = sourceExt;
    } else {
      QBuffer buf(&png);                                  // synthetic original — encode from pixels
      buf.open(QIODevice::WriteOnly);
      orig.save(&buf, "PNG");
    }
    fileStore::ProjectFileData pf;
    pf.name = projectBaseName();
    pf.imageExt = ext;
    pf.imageBytes = png;
    pf.imageWidth = orig.width();
    pf.imageHeight = orig.height();
    pf.source = currentSource;
    pf.resource = currentResource;
    if (const Project* pr = findProject(activeProjectId.toStdString())) {
      pf.color = QString::fromStdString(pr->meta.color);
      for (const auto& k : pr->meta.keywords) pf.keywords << QString::fromStdString(k);
      pf.blank = pr->meta.blank;
      pf.blankColor = QString::fromStdString(pr->meta.blankColor);
    }
    pf.layout = fileStore::buildLayoutJson(
        canvas->imageWidth(), canvas->imageHeight(), canvas->allLines(),
        settings.imageFilter, settings.filterColor,
        canvas->getCropRect(), canvas->getRotationQuarters(), currentLayoutMeta());
    pf.hasTheme = true;
    pf.themeMode = resolveDark(settings.themeMode) ? "dark" : "light";
    pf.themeAccent = settings.accentColor;
    // Persisted chat rides into the portable file only with the opt-in on (§12.3).
    if (settings.saveChatsWithProject && !incognito) pf.chat = buildActiveChatDoc();
    return fileStore::buildProjectFile(pf);
  }

  void MainWindow::saveProjectFileAs() {
    if (!canvas->hasImage()) {
      notify->error("Load an image first");
      return;
    }
    const QString suggested = projectBaseName() + ".stencil";
    const QString path = showSaveDialog(this, "Save project", suggested,
                                        "Stencil project (*.stencil)");
    if (path.isEmpty()) return;
    const QByteArray out = buildStencilBytes();
    if (!writeFileBytes(path, out)) {
      notify->error("Could not write the project file");
      return;
    }
    linkStencilFile(path, out);   // this file becomes the project's live-sync target
    notify->success("Project saved");
  }

  // After a confirm; the project stays open, only the file goes. Mirrors the browser
  // ExportService.deleteProjectFile.
  void MainWindow::deleteProjectFile() {
    if (stencilLink.isEmpty()) {
      notify->error("No linked .stencil file to delete");
      return;
    }
    const QString path = stencilLink;
    const QString shown = QFileInfo(path).fileName();
    // The browser's styled confirm (exportService.js confirmIcon): a bin, red.
    ConfirmSpec spec;
    spec.title = tr("Delete project file");
    spec.message = tr("Delete “%1” from disk? This can’t be undone. The project stays open here.").arg(shown);
    spec.confirmLabel = tr("Delete");
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(this, spec)) { notify->info("Delete canceled"); return; }

    if (!QFile::remove(path)) {
      notify->error("Could not delete the project file");   // keep the link so live-sync survives
      return;
    }
    unlinkStencilFile();   // gone from disk → nothing to sync to
    notify->success(tr("Deleted “%1”").arg(shown));
  }


  void MainWindow::linkStencilFile(const QString& path, const QByteArray& baseline) {
    stencilLink = path;
    stencilBaseline = baseline;
    if (!stencilWatcher) {
      stencilWatcher = new QFileSystemWatcher(this);
      connect(stencilWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) { onStencilFileChanged(); });
    }
    if (!stencilWatcher->files().isEmpty()) stencilWatcher->removePaths(stencilWatcher->files());
    if (stencilLiveSync && !path.isEmpty()) stencilWatcher->addPath(path);
    if (actStencilLiveSync) actStencilLiveSync->setEnabled(!stencilLink.isEmpty());
    if (actDeleteProjectFile) actDeleteProjectFile->setEnabled(!stencilLink.isEmpty());
  }

  // Mirrors the browser StencilSync.unlink(): the project stays open, with no file to sync to.
  void MainWindow::unlinkStencilFile() {
    if (stencilAutosaveTimer) stencilAutosaveTimer->stop();
    if (stencilWatcher && !stencilWatcher->files().isEmpty()) stencilWatcher->removePaths(stencilWatcher->files());
    stencilLink.clear();
    stencilBaseline.clear();
    if (actStencilLiveSync) actStencilLiveSync->setEnabled(false);
    if (actDeleteProjectFile) actDeleteProjectFile->setEnabled(false);
  }

  void MainWindow::scheduleStencilAutosave() {
    if (stencilLink.isEmpty() || !stencilLiveSync || stencilApplying) return;
    if (!stencilAutosaveTimer) {
      stencilAutosaveTimer = new QTimer(this);
      stencilAutosaveTimer->setSingleShot(true);
      connect(stencilAutosaveTimer, &QTimer::timeout, this, &MainWindow::flushStencilAutosave);
    }
    stencilAutosaveTimer->start(800);
  }
}  // namespace stencil::gui

