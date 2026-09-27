#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "popover.hpp"
#include "modalReveal.hpp"
#include "ChatDock.hpp"

// The compact chat popover: its rect, its open path and the status hint it hides.

namespace stencil::gui {

  // Shared by the open and the already-there check, so the two can never disagree.
  QRect DockChrome::compactChatRect(QWidget* anchor) const {
    if (!w.chatDock || !anchor) return {};
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QRect screen = anchor->screen()->availableGeometry();
    return support::popoverRect(anchorRect, w.chatDock->compactDefaultSize(), screen);
  }

  // Browser chat/panel.js FLOAT_DEFAULT/clampFloatRect parity; the insets clear the whole toolbar row.
  QRect DockChrome::defaultChatFloatRect() const {
    if (!w.chatDock) return {};
    const QSize size = w.chatDock->floatingDefaultSize();
    const QRect screen = w.screen() ? w.screen()->availableGeometry()
                                        : QGuiApplication::primaryScreen()->availableGeometry();
    constexpr int INSET_X = 220;
    constexpr int INSET_Y = 160;
    const QPoint at(w.geometry().left() + INSET_X, w.geometry().top() + INSET_Y);
    QRect r(at, size);
    if (r.right() > screen.right()) r.moveRight(screen.right());
    if (r.bottom() > screen.bottom()) r.moveBottom(screen.bottom());
    if (r.left() < screen.left()) r.moveLeft(screen.left());
    if (r.top() < screen.top()) r.moveTop(screen.top());
    return r;
  }

  void DockChrome::openChatCompactNow(QWidget* anchor) {
    if (!w.chatDock || w.tearingDown || !anchor) return;
    stopChatAnim();   // with motion reduced the slide-out may still be pinned
    // Remember the docked layout this popover displaces, so a later full open restores it.
    if (!w.chatDock->isFloating()) {
      const Qt::DockWidgetArea area = w.dockWidgetArea(w.chatDock);
      if (area != Qt::NoDockWidgetArea) chatCompactPrevArea = area;
    }
    w.chatDock->setFloating(true);
    w.chatDock->setGeometry(compactChatRect(anchor));
    support::veilForReveal(*w.chatDock);
    w.chatDock->show();
    // The incoming one flies OUT of the icon; reaching here always means a real open.
    support::revealWindow(*w.chatDock, w.buttonForAction(w.acts.chat));
    w.chatDock->raise();
    w.chatDock->activateWindow();
    w.chatDock->focusInput();
    setChatCompactPopover(true);  // after setFloating: adoption hooks fired above
  }

  // One door for the flag: the dock forbids its title-bar drag while this is set, so a
  // missed clear here would leave the full-shape panel unable to move or re-dock.
  void DockChrome::setChatCompactPopover(bool on) {
    chatCompactPopover = on;
    if (w.chatDock) w.chatDock->setCompactPopover(on);
  }
}  // namespace stencil::gui
