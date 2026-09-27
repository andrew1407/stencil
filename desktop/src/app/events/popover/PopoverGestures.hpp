#pragma once
#include <QPoint>
#include <QRect>
#include <Qt>

class QAction;
class QToolButton;
class QWidget;

namespace stencil::gui {

  class MainWindow;

  // Browser popover.js altHover: an icon's Alt peek and the glide between peeks, the linger poll
  // that keeps an engaged peek open, and a press judged against the open popover.
  class PopoverGestures {
   public:
    explicit PopoverGestures(MainWindow& w) : w(w) {}

    // Browser popover.js altHover parity. Closes a compact chat the glide moved off first.
    void altPeekOpen(QToolButton* btn, QAction* act);
    // The same once Qt has dispatched the Enter that asked for it, and only while Alt is down.
    void altPeekOpenSoon(QToolButton* btn, QAction* act);
    // A peek opening elsewhere closes a compact chat, unless `opener` sits inside it.
    void closeCompactChatFor(QWidget* opener);
    void startLingerPoll();
    void stopLingerPoll();
    // The popover is this window's child, so its frameGeometry() is not a screen rect.
    QRect popoverRectGlobal() const;
    bool handlePopoverPress(QWidget* target, const QPoint& globalPos,
                            Qt::MouseButton button);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
