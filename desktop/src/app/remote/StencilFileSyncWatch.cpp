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

// The .stencil file watcher and external-change merge.

namespace stencil::gui {

  void MainWindow::flushStencilAutosave() {
    if (stencilLink.isEmpty() || !stencilLiveSync || !canvas->hasImage()) return;
    const QByteArray cur = buildStencilBytes();
    if (cur == stencilBaseline) return;   // no local change
    // If the file changed externally since the baseline, route to the change handler instead of
    // clobbering it.
    QByteArray ext;
    if (readFileBytes(stencilLink, ext) && ext != stencilBaseline) {
      onStencilFileChanged(cur);
      return;
    }
    writeStencilNow(cur);   // reuse the bytes we just built (no second PNG re-encode)
    notify->info("Synced to file");
  }

  void MainWindow::writeStencilNow(const QByteArray& prebuilt) {
    if (stencilLink.isEmpty()) return;
    const QByteArray cur = prebuilt.isEmpty() ? buildStencilBytes() : prebuilt;
    QFile wf(stencilLink);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    wf.write(cur);
    wf.close();
    stencilBaseline = cur;
    // QFileSystemWatcher drops a path once its file is replaced.
    if (stencilWatcher && !stencilWatcher->files().contains(stencilLink)) stencilWatcher->addPath(stencilLink);
  }

  void MainWindow::onStencilFileChanged(const QByteArray& prebuilt) {
    if (stencilLink.isEmpty()) return;
    if (stencilWatcher && !stencilWatcher->files().contains(stencilLink)) stencilWatcher->addPath(stencilLink);
    QByteArray ext;
    if (!readFileBytes(stencilLink, ext)) return;
    if (ext.isEmpty() || ext == stencilBaseline) return;   // no external change vs our baseline
    const QByteArray cur = prebuilt.isEmpty() ? buildStencilBytes() : prebuilt;
    if (cur == stencilBaseline) {                          // no local edits → apply theirs
      applyStencilExternal(ext);
      return;
    }
    // Both changed since the baseline: the browser's 3-way choice (confirmModalChoice); "Keep
    // mine" rides the Cancel slot.
    ConfirmSpec spec;
    spec.title = tr("File changed");
    spec.message = tr("“%1” was changed outside the app and conflicts with your unsaved edits.")
                       .arg(QFileInfo(stencilLink).fileName());
    spec.confirmLabel = tr("Take file’s version");
    spec.confirmIcon = QStringLiteral("download");
    spec.altLabel = tr("Merge lines");
    spec.altIcon = QStringLiteral("layers");
    spec.cancelLabel = tr("Keep mine (overwrite file)");
    const ConfirmChoice pick = confirmModalChoice(this, spec);
    if (pick == ConfirmChoice::CONFIRM) applyStencilExternal(ext);
    else if (pick == ConfirmChoice::ALT) applyStencilExternal(ext, /*merge=*/true);
    else writeStencilNow(cur);   // keep mine → overwrite the file (reuse the bytes we built)
  }

  void MainWindow::applyStencilExternal(const QByteArray& text, bool merge) {
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(text, pf, &err)) {
      notify->error("Could not read the changed project file");
      return;
    }
    QImage img;
    if (!img.loadFromData(pf.imageBytes)) return;
    QJsonObject layout = pf.layout;
    if (merge) {
      int w = 0, h = 0;
      const core::Lines fileLines = fileStore::parseLayoutJson(pf.layout, w, h);
      layout["lines"] = fileStore::linesToJson(mergeLinesUnion(fileLines, canvas->allLines()));
    }
    stencilApplying = true;
    loadImageWithLayout(img, layout, pf.imageBytes, pf.imageExt);
    stencilApplying = false;
    if (merge) {
      writeStencilNow();   // push the merged result back to the file
      notify->success("Merged with file");
    } else {
      stencilBaseline = text;
      notify->success("Reloaded from file");
    }
    refreshActions();
  }

  void MainWindow::toggleStencilLiveSync(bool on) {
    stencilLiveSync = on;
    if (on && !stencilLink.isEmpty()) {
      QFile rf(stencilLink);
      if (rf.open(QIODevice::ReadOnly)) { stencilBaseline = rf.readAll(); rf.close(); }
      if (stencilWatcher) stencilWatcher->addPath(stencilLink);
      scheduleStencilAutosave();   // push any pending local edits
      notify->success(tr("Live sync on — auto-saving to %1").arg(QFileInfo(stencilLink).fileName()));
    } else {
      if (stencilWatcher && !stencilWatcher->files().isEmpty()) stencilWatcher->removePaths(stencilWatcher->files());
      if (on) notify->info("Open or save a .stencil file first to enable live sync");
      else notify->info("Live sync off");
    }
  }
}  // namespace stencil::gui

