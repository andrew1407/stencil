#include "MainWindow.hpp"
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "popover.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "SelectionPanel.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"
#include "../support/ShimmerOverlay.hpp"

#include <QGuiApplication>
#include <QTimer>

// The compact chat popover: its rect, its open path and the status hint it hides.

namespace stencil::gui {

  // The chat icon's popover shape (browser chatPanel.js openCompact): the SAME dock, floated at its compact size next to the icon.
  void MainWindow::openChatCompact(QWidget* anchor) {
    if (!chatDock_ || tearingDown_ || !anchor) return;
    stopChatAnim();  // a popover open mid-slide wins outright (setChatShown rule)
    // A chat already on screen in its FULL shape LEAVES through the animated path first, one window at a time.
    // Already pinned where this gesture wants it: raise and focus, no flight.
    if (chatCompactShowing() && chatDock_->geometry() == compactChatRect(anchor)) {
      chatDock_->raise();
      chatDock_->activateWindow();
      chatDock_->focusInput();
      return;
    }
    // Anything else LEAVES first — a compact float that has to move included; teleporting it read as "the chat vanished".
    if (chatDock_->isVisible()) {
      const int outMs = chatDock_->isFloating() ? WINDOW_DISMISS_MS : CHAT_SLIDE_OUT_MS;
      setChatCompactPopover(false);   // it is leaving; the next open re-establishes it
      setChatShown(false, /*animate=*/true);
      QPointer<QWidget> pin(anchor);
      QTimer::singleShot(support::motionReduced() ? 0 : outMs, this, [this, pin] {
        if (pin) openChatCompactNow(pin);
      });
      return;
    }
    openChatCompactNow(anchor);
  }

  // Shared by the open and the already-there check, so the two can never disagree.
  QRect MainWindow::compactChatRect(QWidget* anchor) const {
    if (!chatDock_ || !anchor) return {};
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QRect screen = anchor->screen()->availableGeometry();
    return support::popoverRect(anchorRect, chatDock_->floatingDefaultSize(), screen);
  }

  // Browser chatPanel.js FLOAT_DEFAULT/clampFloatRect parity; the insets clear the whole toolbar row.
  QRect MainWindow::defaultChatFloatRect() const {
    if (!chatDock_) return {};
    const QSize size = chatDock_->floatingDefaultSize();
    const QRect screen = this->screen() ? this->screen()->availableGeometry()
                                        : QGuiApplication::primaryScreen()->availableGeometry();
    constexpr int INSET_X = 220;
    constexpr int INSET_Y = 160;
    const QPoint at(geometry().left() + INSET_X, geometry().top() + INSET_Y);
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
    // Remember the docked layout this popover displaces, so a later full open restores it.
    if (!chatDock_->isFloating()) {
      const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
      if (area != Qt::NoDockWidgetArea) chatCompactPrevArea_ = area;
    }
    chatDock_->setFloating(true);
    chatDock_->setGeometry(compactChatRect(anchor));
    chatDock_->show();
    // The incoming one flies OUT of the icon; reaching here always means a real open.
    support::revealWindow(*chatDock_, buttonForAction(actChat_));
    chatDock_->raise();
    chatDock_->activateWindow();
    chatDock_->focusInput();
    setChatCompactPopover(true);  // after setFloating: adoption hooks fired above
  }

  // One door for the flag: the dock forbids its title-bar drag while this is set, so a
  // missed clear here would leave the full-shape panel unable to move or re-dock.
  void MainWindow::setChatCompactPopover(bool on) {
    chatCompactPopover_ = on;
    if (chatDock_) chatDock_->setCompactPopover(on);
  }

  bool MainWindow::chatCompactShowing() const {
    return chatCompactPopover_ && chatDock_ && chatDock_->isFloating() &&
           chatDock_->isVisible();
  }

  // The "?" is the COLLAPSED state's readout: shown only with the rows hidden AND an image open or incognito on.
  void MainWindow::refreshStatusHintVisibility() {
    // The size line goes with the tool rows; the "?" takes over. Two readouts, one at a time.
    if (imageSizeInfo_) imageSizeInfo_->setVisible(toolbarsShown_);
    if (!statusHintAction_) return;
    const bool live = (canvas_ && canvas_->hasImage()) || incognito_;
    statusHintAction_->setVisible(live && !toolbarsShown_);
  }

}  // namespace stencil::gui
