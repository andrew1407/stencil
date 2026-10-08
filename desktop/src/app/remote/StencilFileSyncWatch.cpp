#include "StencilFileSync.hpp"
#include "mainWindowHelpers.hpp"
#include "../../support/modal/modalChrome.hpp"  // confirmModalChoice — the browser-styled question
#include "Notifications.hpp"
#include "deferredWrite.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>

#include <utility>

// The .stencil file watcher: flushing the auto-save, writing the file, and an external change —
// applied, merged or overwritten.

namespace stencil::gui {

  void StencilFileSync::flushAutosave() {
    if (path.isEmpty() || !liveSync) return;
    if (prompting) { missedWhilePrompting = true; return; }
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
    // Atomic: a watcher or another editor never reads a half-written project.
    if (!deferredWrite::atomic(path, cur)) return;
    baseline = cur;
    // QFileSystemWatcher drops a path once its file is replaced.
    if (watcher && !watcher->files().contains(path)) watcher->addPath(path);
  }

  void StencilFileSync::onFileChanged(const QByteArray& prebuilt) {
    if (path.isEmpty()) return;
    if (watcher && !watcher->files().contains(path)) watcher->addPath(path);
    if (prompting) { missedWhilePrompting = true; return; }
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
    QPointer<StencilFileSync> self(this);
    prompting = true;
    const ConfirmChoice pick = h.choose ? h.choose(host, spec) : confirmModalChoice(host, spec);
    if (!self) return;
    prompting = false;
    if (pick == ConfirmChoice::CONFIRM) h.applyExternal(ext, false);
    else if (pick == ConfirmChoice::ALT) h.applyExternal(ext, /*merge=*/true);
    else writeNow();   // keep mine → overwrite the file with the editor as it is now
    // A change or flush that arrived while the question was open runs against the new baseline.
    if (std::exchange(missedWhilePrompting, false)) {
      onFileChanged();
      flushAutosave();
    }
  }

}  // namespace stencil::gui
