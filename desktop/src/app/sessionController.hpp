#pragma once
#include <QObject>
#include <QTimer>
#include <functional>

namespace stencil::gui {

  // The editor's debounced persistence and write gates; the window supplies the save bodies.
  class SessionController {
   public:
    // Browser parity: storage.js's autosave and scroll/zoom debounce.
    static constexpr int kAutosaveMs = 600;
    static constexpr int kViewSaveMs = 400;

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
      autosave_ = newTimer(owner, std::move(saveSession));
      viewSave_ = newTimer(owner, std::move(saveView));
    }
    // Only incognito stops the timer arming; the remote-unsynced case is decided at fire time.
    void scheduleAutosave(bool autosaveEnabled, const Gates& g) {
      if (autosave_ && autosaveEnabled && !g.incognito) autosave_->start(kAutosaveMs);
    }
    void scheduleViewSave(const Gates& g) {
      if (viewSave_ && wantsViewSchedule(g, restoring_)) viewSave_->start(kViewSaveMs);
    }

    bool restoring() const { return restoring_; }
    void setRestoring(bool on) { restoring_ = on; }

   private:
    static QTimer* newTimer(QObject* owner, std::function<void()> fire) {
      auto* t = new QTimer(owner);
      t->setSingleShot(true);
      QObject::connect(t, &QTimer::timeout, owner, std::move(fire));
      return t;
    }
    QTimer* autosave_ = nullptr;
    QTimer* viewSave_ = nullptr;
    bool restoring_ = false;
  };

}  // namespace stencil::gui
