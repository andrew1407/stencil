#include "mainWindow.hpp"
#include "stencilFileSyncParts.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"  // showSaveDialog
#include "../support/modalChrome.hpp"  // confirmModalChoice — the browser-styled question
#include "iconSet.hpp"
#include "notifications.hpp"
#include "theme.hpp"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSet>
#include <QTimer>

// The .stencil project-file sync block: serialize (buildStencilBytes), Save-As /
// delete, the file link + watcher, debounced auto-save, external-change merge,
// and the live-sync toggle. Split from mainWindow.cpp; same class, definitions only.

namespace stencil::gui {

  QByteArray MainWindow::buildStencilBytes() {
    if (!canvas_->hasImage()) return {};
    const QImage& orig = canvas_->originalImage();
    QByteArray png;
    QString ext = QStringLiteral("png");
    if (!sourceBytes_.isEmpty()) {
      png = sourceBytes_;                                  // untouched original (lossless)
      if (!sourceExt_.isEmpty()) ext = sourceExt_;
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
    pf.source = currentSource_;
    pf.resource = currentResource_;
    if (const Project* pr = findProject(activeProjectId_.toStdString())) {
      pf.color = QString::fromStdString(pr->meta.color);
      for (const auto& k : pr->meta.keywords) pf.keywords << QString::fromStdString(k);
      pf.blank = pr->meta.blank;
      pf.blankColor = QString::fromStdString(pr->meta.blankColor);
    }
    pf.layout = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
        settings_.imageFilter, settings_.filterColor,
        canvas_->cropRect(), canvas_->rotationQuarters(), currentLayoutMeta());
    pf.hasTheme = true;
    pf.themeMode = resolveDark(settings_.themeMode) ? "dark" : "light";
    pf.themeAccent = settings_.accentColor;
    // Persisted chat rides into the portable file only when the opt-in is on
    // (§12.3) — sharing the file then deliberately shares the conversation.
    if (settings_.saveChatsWithProject && !incognito_) pf.chat = buildActiveChatDoc();
    return fileStore::buildProjectFile(pf);
  }

  void MainWindow::saveProjectFileAs() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    const QString suggested = projectBaseName() + ".stencil";
    const QString path = showSaveDialog(this, "Save project", suggested,
                                        "Stencil project (*.stencil)");
    if (path.isEmpty()) return;
    const QByteArray out = buildStencilBytes();
    if (!writeFileBytes(path, out)) {
      notify_->error("Could not write the project file");
      return;
    }
    linkStencilFile(path, out);   // this file becomes the project's live-sync target
    notify_->success("Project saved");
  }

  // Delete the linked .stencil file from disk (after a confirm), then unlink so live-sync stops.
  // The project itself stays open in the editor — only the on-disk file is removed. On a failed
  // remove the link is kept. Mirrors the browser ExportService.deleteProjectFile.
  void MainWindow::deleteProjectFile() {
    if (stencilLink_.isEmpty()) {
      notify_->error("No linked .stencil file to delete");
      return;
    }
    const QString path = stencilLink_;
    const QString shown = QFileInfo(path).fileName();
    // The browser's styled confirm (exportService.js confirmIcon): a delete that
    // can't be undone shows a bin, never the generic tick — and it's red.
    ConfirmSpec spec;
    spec.title = tr("Delete project file");
    spec.message = tr("Delete “%1” from disk? This can’t be undone. The project stays open here.").arg(shown);
    spec.confirmLabel = tr("Delete");
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(this, spec)) { notify_->info("Delete canceled"); return; }

    if (!QFile::remove(path)) {
      notify_->error("Could not delete the project file");   // keep the link so live-sync survives
      return;
    }
    unlinkStencilFile();   // gone from disk → nothing to sync to
    notify_->success(tr("Deleted “%1”").arg(shown));
  }


  void MainWindow::linkStencilFile(const QString& path, const QByteArray& baseline) {
    stencilLink_ = path;
    stencilBaseline_ = baseline;
    if (!stencilWatcher_) {
      stencilWatcher_ = new QFileSystemWatcher(this);
      connect(stencilWatcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString&) { onStencilFileChanged(); });
    }
    if (!stencilWatcher_->files().isEmpty()) stencilWatcher_->removePaths(stencilWatcher_->files());
    if (stencilLiveSync_ && !path.isEmpty()) stencilWatcher_->addPath(path);
    if (actStencilLiveSync_) actStencilLiveSync_->setEnabled(!stencilLink_.isEmpty());
    if (actDeleteProjectFile_) actDeleteProjectFile_->setEnabled(!stencilLink_.isEmpty());
  }

  // Drop the .stencil file link (mirrors the browser StencilSync.unlink()): stop the pending
  // auto-save + the watcher and disable the file-linked actions. The project stays open; there is
  // just no file to sync to anymore.
  void MainWindow::unlinkStencilFile() {
    if (stencilAutosaveTimer_) stencilAutosaveTimer_->stop();
    if (stencilWatcher_ && !stencilWatcher_->files().isEmpty()) stencilWatcher_->removePaths(stencilWatcher_->files());
    stencilLink_.clear();
    stencilBaseline_.clear();
    if (actStencilLiveSync_) actStencilLiveSync_->setEnabled(false);
    if (actDeleteProjectFile_) actDeleteProjectFile_->setEnabled(false);
  }

  void MainWindow::scheduleStencilAutosave() {
    if (stencilLink_.isEmpty() || !stencilLiveSync_ || stencilApplying_) return;
    if (!stencilAutosaveTimer_) {
      stencilAutosaveTimer_ = new QTimer(this);
      stencilAutosaveTimer_->setSingleShot(true);
      connect(stencilAutosaveTimer_, &QTimer::timeout, this, &MainWindow::flushStencilAutosave);
    }
    stencilAutosaveTimer_->start(800);
  }
}  // namespace stencil::gui

