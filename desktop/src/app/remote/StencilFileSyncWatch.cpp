#include "StencilFileSync.hpp"
#include "mainWindowHelpers.hpp"
#include "../../support/modal/modalChrome.hpp"  // confirmModalChoice — the browser-styled question
#include "Notifications.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>

// The .stencil file watcher: flushing the auto-save, writing the file, and an external change —
// applied, merged or overwritten.

namespace stencil::gui {

  void StencilFileSync::flushAutosave() {
    if (path.isEmpty() || !liveSync) return;
    const QByteArray cur = h.build();
    if (cur.isEmpty() || cur == baseline) return;   // no picture, or no local change
    // If the file changed externally since the baseline, route to the change handler instead of
    // clobbering it.
    QByteArray ext;
    if (readFileBytes(path, ext) && ext != baseline) {
      onFileChanged(cur);
      return;
    }
    writeNow(cur);   // reuse the bytes we just built (no second PNG re-encode)
    notify->info("Synced to file");
  }

  void StencilFileSync::writeNow(const QByteArray& prebuilt) {
    if (path.isEmpty()) return;
    const QByteArray cur = prebuilt.isEmpty() ? h.build() : prebuilt;
    QFile wf(path);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    wf.write(cur);
    wf.close();
    baseline = cur;
    // QFileSystemWatcher drops a path once its file is replaced.
    if (watcher && !watcher->files().contains(path)) watcher->addPath(path);
  }

  void StencilFileSync::onFileChanged(const QByteArray& prebuilt) {
    if (path.isEmpty()) return;
    if (watcher && !watcher->files().contains(path)) watcher->addPath(path);
    QByteArray ext;
    if (!readFileBytes(path, ext)) return;
    if (ext.isEmpty() || ext == baseline) return;   // no external change vs our baseline
    const QByteArray cur = prebuilt.isEmpty() ? h.build() : prebuilt;
    if (cur == baseline) {                          // no local edits → apply theirs
      h.applyExternal(ext, false);
      return;
    }
    // Both changed since the baseline: the browser's 3-way choice (confirmModalChoice); "Keep
    // mine" rides the Cancel slot.
    ConfirmSpec spec;
    spec.title = tr("File changed");
    spec.message = tr("“%1” was changed outside the app and conflicts with your unsaved edits.")
                       .arg(QFileInfo(path).fileName());
    spec.confirmLabel = tr("Take file’s version");
    spec.confirmIcon = QStringLiteral("download");
    spec.altLabel = tr("Merge lines");
    spec.altIcon = QStringLiteral("layers");
    spec.cancelLabel = tr("Keep mine (overwrite file)");
    const ConfirmChoice pick = confirmModalChoice(host, spec);
    if (pick == ConfirmChoice::CONFIRM) h.applyExternal(ext, false);
    else if (pick == ConfirmChoice::ALT) h.applyExternal(ext, /*merge=*/true);
    else writeNow(cur);   // keep mine → overwrite the file (reuse the bytes we built)
  }

}  // namespace stencil::gui
