#pragma once
#include <QObject>
#include <QTimer>
#include <functional>

namespace stencil::gui {

  // The editor's debounced persistence and write gates; the window supplies the save bodies.
  class SessionController {
   public:
    // Browser parity: storage.js's autosave and scroll/zoom debounce.
    static constexpr int AUTOSAVE_MS = 600;
    static constexpr int VIEW_SAVE_MS = 400;

    struct Gates {
      // Incognito gates every write of the incognito editor's own state — a desktop-only widening
      // of the browser rule.
      bool incognito = false;
      // A fetched server project with sync off is edited in memory only.
      bool remoteUnsynced = false;
      bool hasActiveProject = false;
      bool hasImage = false;
    };

    static bool wantsSessionWrite(const Gates& g) { return !g.incognito && !g.remoteUnsynced; }
    static bool wantsViewSchedule(const Gates& g, bool restoring) {
      return !g.incognito && g.hasActiveProject && g.hasImage && !restoring;
    }
    static bool wantsViewWrite(const Gates& g, bool restoring) {
      return !g.incognito && g.hasActiveProject && !restoring && !g.remoteUnsynced;
    }
    // A layout-induced scrollbar valueChanged also trips the debounce; without this "Saved" toasts
    // for a resize.
    static bool viewMoved(double zoom, int left, int top, double wasZoom, int wasLeft, int wasTop) {
      return zoom != wasZoom || left != wasLeft || top != wasTop;
    }

    void attach(QObject* owner, std::function<void()> saveSession, std::function<void()> saveView) {
      autosave = newTimer(owner, std::move(saveSession));
      viewSave = newTimer(owner, std::move(saveView));
    }
    // Only incognito stops the timer arming; the remote-unsynced case is decided at fire time.
    void scheduleAutosave(bool autosaveEnabled, const Gates& g) {
      if (autosave && autosaveEnabled && !g.incognito) autosave->start(AUTOSAVE_MS);
    }
    void scheduleViewSave(const Gates& g) {
      if (viewSave && wantsViewSchedule(g, restoring)) viewSave->start(VIEW_SAVE_MS);
    }

    bool getRestoring() const { return restoring; }
    void setRestoring(bool on) { restoring = on; }

   private:
    static QTimer* newTimer(QObject* owner, std::function<void()> fire) {
      auto* t = new QTimer(owner);
      t->setSingleShot(true);
      QObject::connect(t, &QTimer::timeout, owner, std::move(fire));
      return t;
    }
    QTimer* autosave = nullptr;
    QTimer* viewSave = nullptr;
    bool restoring = false;
  };

}  // namespace stencil::gui
