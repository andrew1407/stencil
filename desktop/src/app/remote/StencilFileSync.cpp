#include "StencilFileSync.hpp"
#include "Notifications.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QWidget>

// The .stencil link: bind and unbind the file, the debounced auto-save and the live-sync switch.
// The watcher, the write and the conflict choice are in StencilFileSyncWatch.cpp.

namespace stencil::gui {

  StencilFileSync::StencilFileSync(QWidget* host, QPointer<Notifications> notify, Hooks hooks)
      : QObject(host), host(host), notify(std::move(notify)), h(std::move(hooks)) {}

  void StencilFileSync::link(const QString& file, const QByteArray& baseline) {
    path = file;
    this->baseline = baseline;
    if (!watcher) {
      watcher = new QFileSystemWatcher(this);
      connect(watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) { onFileChanged(); });
    }
    if (!watcher->files().isEmpty()) watcher->removePaths(watcher->files());
    if (liveSync && !file.isEmpty()) watcher->addPath(file);
    if (h.linkChanged) h.linkChanged(!path.isEmpty());
  }

  void StencilFileSync::unlink() {
    if (autosaveTimer) autosaveTimer->stop();
    if (watcher && !watcher->files().isEmpty()) watcher->removePaths(watcher->files());
    path.clear();
    baseline.clear();
    if (h.linkChanged) h.linkChanged(false);
  }

  void StencilFileSync::scheduleAutosave() {
    if (path.isEmpty() || !liveSync || applying) return;
    if (!autosaveTimer) {
      autosaveTimer = new QTimer(this);
      autosaveTimer->setSingleShot(true);
      connect(autosaveTimer, &QTimer::timeout, this, &StencilFileSync::flushAutosave);
    }
    autosaveTimer->start(800);
  }

  bool StencilFileSync::autosavePending() const { return autosaveTimer && autosaveTimer->isActive(); }

  void StencilFileSync::setLiveSync(bool on) {
    liveSync = on;
    if (on && !path.isEmpty()) {
      QFile rf(path);
      if (rf.open(QIODevice::ReadOnly)) { baseline = rf.readAll(); rf.close(); }
      if (watcher) watcher->addPath(path);
      scheduleAutosave();   // push any pending local edits
      notify->success(tr("Live sync on — auto-saving to %1").arg(QFileInfo(path).fileName()));
    } else {
      if (watcher && !watcher->files().isEmpty()) watcher->removePaths(watcher->files());
      if (on) notify->info("Open or save a .stencil file first to enable live sync");
      else notify->info("Live sync off");
    }
  }

}  // namespace stencil::gui
