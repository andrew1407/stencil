#pragma once
// The Alt+hover peek of one popup-shaped window and the glide registry every peek shares — the
// desktop port of browser/js/ui/tip/popover.js createModalOpenGesture (its altHover / altRelease
// / boxEnter / boxLeave / notifyClosed half) as ui/tip/altPeek.js drives it for a dropdown.
#include <QObject>
#include <QTimer>
#include <functional>

class QWidget;

namespace stencil::support {

  // Set on a widget whose own Alt+Enter opens a peek (a popover icon), so a glide can reach it.
  inline constexpr const char* ALT_PEEK_TARGET_PROPERTY = "altPeekTarget";

  // `opener` is the widget the new peek grows out of; a handle leaves a window that contains it.
  using GlideCloser = std::function<void(QWidget* opener)>;
  // Dropped when `owner` is destroyed.
  void addGlideHandle(QObject* owner, GlideCloser close);
  // Every handle but `self`'s closes: a new peek is opening.
  void glideFrom(const QObject* self, QWidget* opener);

  class AltPeekGesture : public QObject {
   public:
    // NONE → nothing of ours is open or it was opened deliberately (a click); a click-open
    // list ignores Alt entirely.
    enum class Mode { NONE, PEEK, LINGER };
    struct Deps {
      std::function<void()> open, close;
      std::function<bool()> isOpen;
      std::function<bool()> isEngaged;   // the pointer is over the open list
    };
    AltPeekGesture(Deps deps, QObject* parent);

    void altHover(QWidget* opener);
    // Blur too — Alt+Tab eats the keyup. An engaged peek lingers until boxLeave closes it.
    void altRelease();
    void boxEnter();
    void boxLeave();
    // The owner closed the list its own way (a pick, Escape, an outside press).
    void notifyClosed();
    Mode mode() const { return state; }

   private:
    void closeOwn();

    Deps deps;
    Mode state = Mode::NONE;
    QTimer linger;
  };

}  // namespace stencil::support
