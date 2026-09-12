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

  // Alt+hover peek (browser popover.js altHover parity); never adopts the machine's own open
  // window.
  void MainWindow::altPeekOpen(QToolButton* btn, QAction* act) {
    if (!btn || !act || !act->isEnabled() || pop_.active) return;
    // Another dialog is up: a peek under it would be unreachable.
    if (QApplication::activeModalWidget()) return;
    // Gliding off an open compact chat closes it; a docked panel or a user-chosen float is never
    // touched.
    if (act != actChat_ && actChat_ && actChat_->isChecked() && chatCompactShowing()) {
      pop_.peekAction.clear();
      actChat_->setChecked(false);
    }
    stopLingerPoll();
    if (pop_.clickTimer) pop_.clickTimer->stop();
    pop_.pendingAction.clear();
    // A chat already on screen takes the same animated swap the right-click route does, but is
    // never adopted as a peek.
    if (act == actChat_ && actChat_->isChecked()) {
      pop_.anchor.clear();
      pop_.peekAction.clear();
      openChatCompact(btn);
      return;
    }
    pop_.anchor = btn;
    pop_.peekAction = act;
    act->trigger();
    // exec() blocks until the dialog closes, so reaching here ends the peek; only the non-blocking
    // chat keeps its flag until Alt release.
    if (act != actChat_) pop_.peekAction.clear();
  }

  // Linger poll: close once the pointer is outside, unless a field or the composer holds typed
  // content.
  void MainWindow::startLingerPoll() {
    if (!pop_.lingerPoll) {
      pop_.lingerPoll = new QTimer(this);
      pop_.lingerPoll->setInterval(120);
      connect(pop_.lingerPoll, &QTimer::timeout, this, [this] {
        QWidget* w = pop_.active
            ? static_cast<QWidget*>(pop_.active.data())
            : ((chatDock_ && chatDock_->isFloating() && chatDock_->isVisible()) ? chatDock_ : nullptr);
        if (!w) { pop_.lingerPoll->stop(); return; }
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

  // exec() a dialog centred, or as a compact popover when a gesture armed pop_.anchor; a nested
  // dialog is not "outside".
  // Fade the window out, then reject: the shrink-into-icon ghost shows only after unmap, so this
  // avoids a blink. pop_.active drops first (re-entrancy).
  void MainWindow::dismissPopover() {
    if (!pop_.active) return;
    // The linger poll would otherwise spend the closing animation watching the floating chat dock.
    stopLingerPoll();
    // reject() is the whole dismissal; execMaybePopover's finished() handler owns the collapse.
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
    // The popover lives inside this window, so this test tells inside from outside.
    f.insidePopover = popoverRectGlobal().contains(globalPos);
    if (target) {
      // A press in a nested dialog (a confirm, a native picker) is left alone.
      f.otherWindowOwns = target->window() != this;
    } else {
      // Polled press: any other visible top-level owns that click.
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
    // The press travels on, but must not re-open what it just dismissed — the popover is non-
    // modal, so it reaches the button.
    f.onPopoverIcon = target && (target == logoBtn_ || pop_.buttons.contains(target));

    const auto verdict = PopoverHost::judgePress(f, button);
    if (verdict.clearPeek) pop_.peekAction.clear();
    if (verdict.dismiss) dismissPopover();
    if (verdict.armDismissClick) pop_.dismissClick = true;
    return verdict.consume;
  }

}  // namespace stencil::gui
