#include "mainWindow.hpp"
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

  // ── .stencil live sync ───────────────────────────────────────────────────────
  namespace {
    // Union two line lists, de-duplicating by the compact JSON of each line (a merge that keeps
    // both editors' annotations without duplicating a round-tripped twin — mirrors browser mergeLines).
    core::Lines mergeLinesUnion(const core::Lines& base, const core::Lines& extra) {
      core::Lines out = base;
      QSet<QString> seen;
      auto keyOf = [](const core::Line& l) {
        return QString::fromUtf8(QJsonDocument(fileStore::lineToJson(l)).toJson(QJsonDocument::Compact));
      };
      for (const auto& l : base) seen.insert(keyOf(l));
      for (const auto& l : extra) {
        const QString k = keyOf(l);
        if (!seen.contains(k)) { out.push_back(l); seen.insert(k); }
      }
      return out;
    }
  }  // namespace

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

  void MainWindow::flushStencilAutosave() {
    if (stencilLink_.isEmpty() || !stencilLiveSync_ || !canvas_->hasImage()) return;
    const QByteArray cur = buildStencilBytes();
    if (cur == stencilBaseline_) return;   // no local change
    // Race: if the file changed externally since our baseline, route to the change handler
    // (apply / prompt) instead of clobbering it — reusing `cur` so it needn't rebuild them.
    QByteArray ext;
    if (readFileBytes(stencilLink_, ext) && ext != stencilBaseline_) {
      onStencilFileChanged(cur);
      return;
    }
    writeStencilNow(cur);   // reuse the bytes we just built (no second PNG re-encode)
    notify_->info("Synced to file");
  }

  void MainWindow::writeStencilNow(const QByteArray& prebuilt) {
    if (stencilLink_.isEmpty()) return;
    const QByteArray cur = prebuilt.isEmpty() ? buildStencilBytes() : prebuilt;
    QFile wf(stencilLink_);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    wf.write(cur);
    wf.close();
    stencilBaseline_ = cur;
    // QFileSystemWatcher drops a path once its file is replaced — re-add so we keep watching.
    if (stencilWatcher_ && !stencilWatcher_->files().contains(stencilLink_)) stencilWatcher_->addPath(stencilLink_);
  }

  void MainWindow::onStencilFileChanged(const QByteArray& prebuilt) {
    if (stencilLink_.isEmpty()) return;
    if (stencilWatcher_ && !stencilWatcher_->files().contains(stencilLink_)) stencilWatcher_->addPath(stencilLink_);
    QByteArray ext;
    if (!readFileBytes(stencilLink_, ext)) return;
    if (ext.isEmpty() || ext == stencilBaseline_) return;   // no external change vs our baseline
    const QByteArray cur = prebuilt.isEmpty() ? buildStencilBytes() : prebuilt;
    if (cur == stencilBaseline_) {                          // no local edits → apply theirs
      applyStencilExternal(ext);
      return;
    }
    // Conflict: both changed since the baseline — the browser's styled 3-way choice
    // (confirmModalChoice): take the file's copy, stack both line sets, or keep
    // yours. "Keep mine" rides the Cancel slot — it is the do-nothing-to-the-editor
    // answer — with the browser's glyphs on the two real actions.
    ConfirmSpec spec;
    spec.title = tr("File changed");
    spec.message = tr("“%1” was changed outside the app and conflicts with your unsaved edits.")
                       .arg(QFileInfo(stencilLink_).fileName());
    spec.confirmLabel = tr("Take file’s version");
    spec.confirmIcon = QStringLiteral("download");
    spec.altLabel = tr("Merge lines");
    spec.altIcon = QStringLiteral("layers");
    spec.cancelLabel = tr("Keep mine (overwrite file)");
    const ConfirmChoice pick = confirmModalChoice(this, spec);
    if (pick == ConfirmChoice::Confirm) applyStencilExternal(ext);
    else if (pick == ConfirmChoice::Alt) applyStencilExternal(ext, /*merge=*/true);
    else writeStencilNow(cur);   // keep mine → overwrite the file (reuse the bytes we built)
  }

  void MainWindow::applyStencilExternal(const QByteArray& text, bool merge) {
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(text, pf, &err)) {
      notify_->error("Could not read the changed project file");
      return;
    }
    QImage img;
    if (!img.loadFromData(pf.imageBytes)) return;
    QJsonObject layout = pf.layout;
    if (merge) {
      int w = 0, h = 0;
      const core::Lines fileLines = fileStore::parseLayoutJson(pf.layout, w, h);
      layout["lines"] = fileStore::linesToJson(mergeLinesUnion(fileLines, canvas_->allLines()));
    }
    stencilApplying_ = true;
    loadImageWithLayout(img, layout, pf.imageBytes, pf.imageExt);
    stencilApplying_ = false;
    if (merge) {
      writeStencilNow();   // push the merged result back to the file
      notify_->success("Merged with file");
    } else {
      stencilBaseline_ = text;
      notify_->success("Reloaded from file");
    }
    refreshActions();
  }

  void MainWindow::toggleStencilLiveSync(bool on) {
    stencilLiveSync_ = on;
    if (on && !stencilLink_.isEmpty()) {
      QFile rf(stencilLink_);
      if (rf.open(QIODevice::ReadOnly)) { stencilBaseline_ = rf.readAll(); rf.close(); }
      if (stencilWatcher_) stencilWatcher_->addPath(stencilLink_);
      scheduleStencilAutosave();   // push any pending local edits
      notify_->success(tr("Live sync on — auto-saving to %1").arg(QFileInfo(stencilLink_).fileName()));
    } else {
      if (stencilWatcher_ && !stencilWatcher_->files().isEmpty()) stencilWatcher_->removePaths(stencilWatcher_->files());
      if (on) notify_->info("Open or save a .stencil file first to enable live sync");
      else notify_->info("Live sync off");
    }
  }

}  // namespace stencil::gui
