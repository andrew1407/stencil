#include "mainWindow.hpp"
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "popover.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QGuiApplication>
#include <QTimer>

// The compact chat popover: its rect, its open path and the status hint it hides.

namespace stencil::gui {

  // The chat icon's popover shape (browser chatPanel.js openCompact parity): the
  // SAME dock — same conversation, same attachments — floated at its compact
  // tear-off size and pinned next to the icon by the shared popover placement.
  // Idempotent while already floating there; a docked/hidden chat is torn off.
  void MainWindow::openChatCompact(QWidget* anchor) {
    if (!chatDock_ || tearingDown_ || !anchor) return;
    stopChatAnim();  // a popover open mid-slide wins outright (setChatShown rule)
    // Swapping shapes is a popover swap like any other: a chat already on screen
    // in its FULL shape (docked, or a float the user tore off) LEAVES through the
    // animated path — sliding back into its edge, or flying into the icon — and
    // the compact one opens once that has played, one window at a time. Without
    // this the outgoing window simply vanished under setFloating() below.
    // Already pinned exactly where this gesture wants it: raise and focus, and
    // never re-play a flight for a window that does not move.
    if (chatCompactShowing() && chatDock_->geometry() == compactChatRect(anchor)) {
      chatDock_->raise();
      chatDock_->activateWindow();
      chatDock_->focusInput();
      return;
    }
    // Anything else on screen LEAVES first — including a compact float that has to
    // move (the user dragged it, or another icon anchors it now). Teleporting that
    // window read as "the chat vanished", which is the bug this branch exists for.
    if (chatDock_->isVisible()) {
      const int outMs = chatDock_->isFloating() ? kWindowDismissMs : kChatSlideOutMs;
      chatCompactPopover_ = false;   // it is leaving; the next open re-establishes it
      setChatShown(false, /*animate=*/true);
      QPointer<QWidget> pin(anchor);
      QTimer::singleShot(support::motionReduced() ? 0 : outMs, this, [this, pin] {
        if (pin) openChatCompactNow(pin);
      });
      return;
    }
    openChatCompactNow(anchor);
  }

  // Where the compact popover sits for `anchor` (global): the shared popover
  // placement at the dock's own tear-off size. Shared by the open and the
  // already-there check above, so the two can never disagree.
  QRect MainWindow::compactChatRect(QWidget* anchor) const {
    if (!chatDock_ || !anchor) return {};
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QRect screen = anchor->screen()->availableGeometry();
    return support::popoverRect(anchorRect, chatDock_->floatingDefaultSize(), screen);
  }

  // Browser chatPanel.js FLOAT_DEFAULT/clampFloatRect parity: the Float button's first
  // landing spot — a fixed inset from this window's top-left, clamped onto its screen.
  // The insets clear the whole toolbar row (the browser's own 80px landed on it).
  QRect MainWindow::defaultChatFloatRect() const {
    if (!chatDock_) return {};
    const QSize size = chatDock_->floatingDefaultSize();
    const QRect screen = this->screen() ? this->screen()->availableGeometry()
                                        : QGuiApplication::primaryScreen()->availableGeometry();
    constexpr int kInsetX = 220;
    constexpr int kInsetY = 160;
    const QPoint at(geometry().left() + kInsetX, geometry().top() + kInsetY);
    QRect r(at, size);
    if (r.right() > screen.right()) r.moveRight(screen.right());
    if (r.bottom() > screen.bottom()) r.moveBottom(screen.bottom());
    if (r.left() < screen.left()) r.moveLeft(screen.left());
    if (r.top() < screen.top()) r.moveTop(screen.top());
    return r;
  }

  void MainWindow::openChatCompactNow(QWidget* anchor) {
    if (!chatDock_ || tearingDown_ || !anchor) return;
    stopChatAnim();   // with motion reduced the slide-out may still be pinned
    // Remember the docked layout this popover displaces, so a later full open
    // (toolbar single click / hotkey) restores it instead of the popover rect.
    if (!chatDock_->isFloating()) {
      const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
      if (area != Qt::NoDockWidgetArea) chatCompactPrevArea_ = area;
    }
    chatDock_->setFloating(true);
    chatDock_->setGeometry(compactChatRect(anchor));
    chatDock_->show();
    // …and the incoming one flies OUT of the icon, the same motion every other
    // popover opens with (the caller above already returned for a window that is
    // staying put, so reaching here always means a real open).
    support::revealWindow(*chatDock_, buttonForAction(actChat_));
    chatDock_->raise();
    chatDock_->activateWindow();
    chatDock_->focusInput();
    chatCompactPopover_ = true;  // after setFloating: adoption hooks fired above
  }

  bool MainWindow::chatCompactShowing() const {
    return chatCompactPopover_ && chatDock_ && chatDock_->isFloating() &&
           chatDock_->isVisible();
  }

  // The "?" is the COLLAPSED state's readout: while the tool rows are up they already show
  // the image size, so it would just repeat them. Shown only with the rows hidden AND
  // something worth reading — an image open, or incognito on.
  void MainWindow::refreshStatusHintVisibility() {
    // The size line belongs to the tool rows: it goes with them, and the "?" takes over as
    // the place those facts can still be read. Two readouts of the same thing, one at a time.
    if (imageSizeInfo_) imageSizeInfo_->setVisible(toolbarsShown_);
    if (!statusHintAction_) return;
    const bool live = (canvas_ && canvas_->hasImage()) || incognito_;
    statusHintAction_->setVisible(live && !toolbarsShown_);
  }

}  // namespace stencil::gui
