#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "chatMenuPanel.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
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
#include "../support/appTooltip.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/modalChrome.hpp"

#include <QAction>
#include <QApplication>
#include <QTimer>
#include <QToolButton>

// Alt-peek popovers: the linger poll, the outside-press test and dismissal.

namespace stencil::gui {

  // The Alt+hover peek: open `act`'s popover pinned to `btn` (browser popover.js
  // altHover parity). Closes a floating compact chat the glide is moving off;
  // never adopts the machine's own open window.
  void MainWindow::altPeekOpen(QToolButton* btn, QAction* act) {
    if (!btn || !act || !act->isEnabled() || activePopover_) return;
    // Some other dialog is up (the popover's own boxes are child widgets, never modal):
    // a peek under it would be unreachable, and would paint below it.
    if (QApplication::activeModalWidget()) return;
    // Gliding off an open COMPACT chat (peek, linger, or sticky popover) closes
    // it; a docked chat panel — or a float the user chose (tear-off, the title
    // bar's float button) — is never touched (chatCompactShowing).
    if (act != actChat_ && actChat_ && actChat_->isChecked() && chatCompactShowing()) {
      altPeekAction_.clear();
      actChat_->setChecked(false);
    }
    stopLingerPoll();
    if (popoverClickTimer_) popoverClickTimer_->stop();
    popoverPendingAction_.clear();
    // A chat already on screen: the gesture still means "show it compact HERE",
    // so it takes the same animated swap the right-click route does (it used to
    // return early, leaving the window where it was). Never adopted as a peek,
    // though — the Alt release must not close what the user opened deliberately.
    if (act == actChat_ && actChat_->isChecked()) {
      popoverAnchor_.clear();
      altPeekAction_.clear();
      openChatCompact(btn);
      return;
    }
    popoverAnchor_ = btn;
    altPeekAction_ = act;
    act->trigger();
    // A modal dialog blocks in exec() until it closes, so reaching here means
    // the peek is over — only the NON-blocking chat keeps its flag until the
    // Alt release (or a later deliberate gesture) consumes it.
    if (act != actChat_) altPeekAction_.clear();
  }

  // A LINGERING window (an engaged peek whose Alt was released): poll the cursor
  // and close it once the pointer is outside — unless a field inside holds typed
  // content, or the chat composer does (never yank a window mid-typing).
  void MainWindow::startLingerPoll() {
    if (!lingerPoll_) {
      lingerPoll_ = new QTimer(this);
      lingerPoll_->setInterval(120);
      connect(lingerPoll_, &QTimer::timeout, this, [this] {
        QWidget* w = activePopover_
            ? static_cast<QWidget*>(activePopover_.data())
            : ((chatDock_ && chatDock_->isFloating() && chatDock_->isVisible()) ? chatDock_ : nullptr);
        if (!w) { lingerPoll_->stop(); return; }
        // The popover is a child widget, so ask the overlay where it is on screen.
        const QRect box = activePopover_ ? popoverRectGlobal() : w->frameGeometry();
        if (box.contains(QCursor::pos())) return;
        if (typedContentInside(w)) return;
        if (w == chatDock_ && chatDock_->hasComposerText()) return;
        lingerPoll_->stop();
        if (activePopover_) dismissPopover();
        else if (actChat_ && actChat_->isChecked()) actChat_->setChecked(false);
      });
    }
    lingerPoll_->start();
  }
  void MainWindow::stopLingerPoll() { if (lingerPoll_) lingerPoll_->stop(); }

  // exec() a dialog — centred window, or (when a gesture armed popoverAnchor_) a
  // compact popover pinned to that icon; call sites read exec()'s return unchanged.
  // Outside-click rejects via the app filter, but a NESTED dialog is not "outside".
  // Close the open popover by fading the WINDOW out, then rejecting — the
  // shrink-into-icon ghost only shows after the window unmaps, so fading avoids
  // a close/come-back blink. activePopover_ is dropped up front (re-entrancy).
  void MainWindow::dismissPopover() {
    if (!activePopover_) return;
    // Whatever was watching it has nothing left to watch — and the linger poll would
    // otherwise spend the closing animation looking at the floating chat dock instead.
    stopLingerPoll();
    // reject() is the whole dismissal: execMaybePopover's finished() handler owns the
    // collapse animation, so every route out closes exactly the same way.
    activePopover_->reject();
  }

  QRect MainWindow::popoverRectGlobal() const {
    if (popoverOverlay_)
      return QRect(popoverOverlay_->mapToGlobal(QPoint(0, 0)), popoverOverlay_->size());
    return activePopover_ ? activePopover_->frameGeometry() : QRect();
  }

  bool MainWindow::handlePopoverPress(QWidget* target, const QPoint& globalPos,
                                      Qt::MouseButton button) {
    if (!activePopover_) return false;
    // Inside the popover itself is not "outside" — and it lives INSIDE this window now,
    // so this test, not the window it belongs to, is what tells the two apart.
    if (popoverRectGlobal().contains(globalPos)) return false;
    if (target) {
      // Delivered press: a press in a NESTED dialog (a confirm, a native picker)
      // belongs to another window and is left alone, so flows launched from inside
      // the popover keep working.
      if (target->window() != this) return false;
    } else {
      // Polled press: the same exemption, decided by geometry — any other visible
      // top-level (a nested dialog, a menu) owns that click.
      for (QWidget* w : QApplication::topLevelWidgets())
        if (w != this && w != activePopover_.data() && w->isVisible() &&
            w->frameGeometry().contains(globalPos))
          return false;
    }
    // Gestures on the LOGO while its accent popover is up never dismiss it (browser
    // parity): a RIGHT-press promotes a peek to sticky, a LEFT-press mid-peek is a no-op
    // (both consumed), and a LEFT-press on a sticky popover travels on to the logo, whose
    // click cycles the accent under the open list. Every other press dismisses.
    const bool onLogo =
        logoBtn_ && (target ? target == logoBtn_
                            : QRect(logoBtn_->mapToGlobal(QPoint(0, 0)), logoBtn_->size())
                                  .contains(globalPos));
    if (onLogo && activePopover_->objectName() == QLatin1String("accentPopover")) {
      const bool peeking = altPeekAction_.data() == actAccent_;
      if (button == Qt::RightButton) { altPeekAction_.clear(); return true; }
      if (button == Qt::LeftButton) return peeking;
    }
    dismissPopover();
    // The press travels on (it always did), but the icon it landed on must not
    // re-OPEN what this click just dismissed — a real popup swallows its closing
    // click, and with the popover non-modal that gesture now actually reaches the
    // button. Consumed by the logo's click-cycle and the popover icons' deferred
    // click; harmless if no such gesture follows.
    if (target && (target == logoBtn_ || popoverButtons_.contains(target)))
      popoverDismissClick_ = true;
    return false;
  }

}  // namespace stencil::gui
