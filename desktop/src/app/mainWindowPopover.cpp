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
    if (!btn || !act || !act->isEnabled() || pop_.active) return;
    // Some other dialog is up (the popover's own boxes are child widgets, never modal):
    // a peek under it would be unreachable, and would paint below it.
    if (QApplication::activeModalWidget()) return;
    // Gliding off an open COMPACT chat (peek, linger, or sticky popover) closes
    // it; a docked chat panel — or a float the user chose (tear-off, the title
    // bar's float button) — is never touched (chatCompactShowing).
    if (act != actChat_ && actChat_ && actChat_->isChecked() && chatCompactShowing()) {
      pop_.peekAction.clear();
      actChat_->setChecked(false);
    }
    stopLingerPoll();
    if (pop_.clickTimer) pop_.clickTimer->stop();
    pop_.pendingAction.clear();
    // A chat already on screen: the gesture still means "show it compact HERE",
    // so it takes the same animated swap the right-click route does (it used to
    // return early, leaving the window where it was). Never adopted as a peek,
    // though — the Alt release must not close what the user opened deliberately.
    if (act == actChat_ && actChat_->isChecked()) {
      pop_.anchor.clear();
      pop_.peekAction.clear();
      openChatCompact(btn);
      return;
    }
    pop_.anchor = btn;
    pop_.peekAction = act;
    act->trigger();
    // A modal dialog blocks in exec() until it closes, so reaching here means
    // the peek is over — only the NON-blocking chat keeps its flag until the
    // Alt release (or a later deliberate gesture) consumes it.
    if (act != actChat_) pop_.peekAction.clear();
  }

  // A LINGERING window (an engaged peek whose Alt was released): poll the cursor
  // and close it once the pointer is outside — unless a field inside holds typed
  // content, or the chat composer does (never yank a window mid-typing).
  void MainWindow::startLingerPoll() {
    if (!pop_.lingerPoll) {
      pop_.lingerPoll = new QTimer(this);
      pop_.lingerPoll->setInterval(120);
      connect(pop_.lingerPoll, &QTimer::timeout, this, [this] {
        QWidget* w = pop_.active
            ? static_cast<QWidget*>(pop_.active.data())
            : ((chatDock_ && chatDock_->isFloating() && chatDock_->isVisible()) ? chatDock_ : nullptr);
        if (!w) { pop_.lingerPoll->stop(); return; }
        // The popover is a child widget, so ask the overlay where it is on screen.
        const QRect box = pop_.active ? popoverRectGlobal() : w->frameGeometry();
        if (box.contains(QCursor::pos())) return;
        if (typedContentInside(w)) return;
        if (w == chatDock_ && chatDock_->hasComposerText()) return;
        pop_.lingerPoll->stop();
        if (pop_.active) dismissPopover();
        else if (actChat_ && actChat_->isChecked()) actChat_->setChecked(false);
      });
    }
    pop_.lingerPoll->start();
  }
  void MainWindow::stopLingerPoll() { if (pop_.lingerPoll) pop_.lingerPoll->stop(); }

  // exec() a dialog — centred window, or (when a gesture armed pop_.anchor) a
  // compact popover pinned to that icon; call sites read exec()'s return unchanged.
  // Outside-click rejects via the app filter, but a NESTED dialog is not "outside".
  // Close the open popover by fading the WINDOW out, then rejecting — the
  // shrink-into-icon ghost only shows after the window unmaps, so fading avoids
  // a close/come-back blink. pop_.active is dropped up front (re-entrancy).
  void MainWindow::dismissPopover() {
    if (!pop_.active) return;
    // Whatever was watching it has nothing left to watch — and the linger poll would
    // otherwise spend the closing animation looking at the floating chat dock instead.
    stopLingerPoll();
    // reject() is the whole dismissal: execMaybePopover's finished() handler owns the
    // collapse animation, so every route out closes exactly the same way.
    pop_.active->reject();
  }

  QRect MainWindow::popoverRectGlobal() const {
    if (pop_.overlay)
      return QRect(pop_.overlay->mapToGlobal(QPoint(0, 0)), pop_.overlay->size());
    return pop_.active ? pop_.active->frameGeometry() : QRect();
  }

  bool MainWindow::handlePopoverPress(QWidget* target, const QPoint& globalPos,
                                      Qt::MouseButton button) {
    if (!pop_.active) return false;
    PopoverHost::PressFacts f;
    // Inside the popover itself is not "outside" — and it lives INSIDE this window now,
    // so this test, not the window it belongs to, is what tells the two apart.
    f.insidePopover = popoverRectGlobal().contains(globalPos);
    if (target) {
      // Delivered press: a press in a NESTED dialog (a confirm, a native picker)
      // belongs to another window and is left alone, so flows launched from inside
      // the popover keep working.
      f.otherWindowOwns = target->window() != this;
    } else {
      // Polled press: the same exemption, decided by geometry — any other visible
      // top-level (a nested dialog, a menu) owns that click.
      for (QWidget* w : QApplication::topLevelWidgets())
        if (w != this && w != pop_.active.data() && w->isVisible() &&
            w->frameGeometry().contains(globalPos))
          f.otherWindowOwns = true;
    }
    f.onLogo =
        logoBtn_ && (target ? target == logoBtn_
                            : QRect(logoBtn_->mapToGlobal(QPoint(0, 0)), logoBtn_->size())
                                  .contains(globalPos));
    f.accentPopover = pop_.active->objectName() == QLatin1String("accentPopover");
    f.peekingAccent = pop_.peekAction.data() == actAccent_;
    // The press travels on after a dismissal (it always did), but the icon it landed on
    // must not re-OPEN what this click just dismissed — a real popup swallows its closing
    // click, and with the popover non-modal that gesture now actually reaches the button.
    f.onPopoverIcon = target && (target == logoBtn_ || pop_.buttons.contains(target));

    const auto verdict = PopoverHost::judgePress(f, button);
    if (verdict.clearPeek) pop_.peekAction.clear();
    if (verdict.dismiss) dismissPopover();
    if (verdict.armDismissClick) pop_.dismissClick = true;
    return verdict.consume;
  }

}  // namespace stencil::gui
