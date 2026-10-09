#pragma once
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

#include "../../support/modal/modalChrome.hpp"

class QFileSystemWatcher;
class QTimer;
class QWidget;

namespace stencil::gui {

  class Notifications;

  // Opt-in live sync of the open project to a linked .stencil file, the browser's StencilSync: the
  // link and its baseline, the watcher, the debounced auto-save and the three-way conflict choice.
  // Building and loading the project stay on MainWindow as hooks.
  class StencilFileSync : public QObject {
    Q_OBJECT
   public:
    struct Hooks {
      // The open project as .stencil bytes; empty with no image.
      std::function<QByteArray()> build;
      std::function<void(const QByteArray& text, bool merge)> applyExternal;
      std::function<void(bool linked)> linkChanged;
      // The three-way conflict question; empty asks through confirmModalChoice.
      std::function<ConfirmChoice(QWidget* host, const ConfirmSpec& spec)> choose;
    };

    StencilFileSync(QWidget* host, QPointer<Notifications> notify, Hooks hooks);

    void link(const QString& file, const QByteArray& baseline);
    // The project stays open, with no file to sync to (browser StencilSync.unlink).
    void unlink();
    void writeNow(const QByteArray& prebuilt = {});
    void scheduleAutosave();
    bool autosavePending() const;
    void flushAutosave();
    void onFileChanged(const QByteArray& prebuilt = {});
    void setLiveSync(bool on);

    const QString& linkedPath() const { return path; }
    void adoptBaseline(const QByteArray& text) { baseline = text; }
    // True while an external version is loading, so the edit it causes is not saved back.
    void setApplying(bool on) { applying = on; }

   private:
    void watchLink();

    QWidget* host;
    QPointer<Notifications> notify;
    Hooks h;
    QString path;
    QByteArray baseline;
    bool liveSync = false;
    bool applying = false;
    bool prompting = false;   // the conflict question is open; a change or flush meanwhile is re-run after it
    bool missedWhilePrompting = false;
    QFileSystemWatcher* watcher = nullptr;
    QTimer* autosaveTimer = nullptr;
  };

}  // namespace stencil::gui
