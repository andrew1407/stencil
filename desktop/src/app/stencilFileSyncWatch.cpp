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

