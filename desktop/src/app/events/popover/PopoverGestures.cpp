#include "MainWindow.hpp"
#include "PopoverGestures.hpp"
#include "mainWindowHelpers.hpp"
#include "ShortcutsDialog.hpp"
#include "../../../support/tip/altPeek.hpp"
#include "../../../support/menu/comboAltPeek.hpp"
#include "../../../support/motionPrefs.hpp"
#include "ChatDock.hpp"

// Alt-peek popovers: the linger poll, the outside-press test and dismissal.

namespace stencil::gui {

  // Alt+hover peek (browser popover.js altHover parity); never adopts the machine's own open
  // window.
  void PopoverGestures::altPeekOpen(QToolButton* btn, QAction* act) {
    if (!btn || !act || !act->isEnabled() || w.pop.active) return;
    // Another dialog is up: a peek under it would be unreachable.
    if (QApplication::activeModalWidget()) return;
    support::glideFrom(&w, btn);   // a selector's peek closes as the icon's opens
    if (act != w.acts.chat) closeCompactChatFor(btn);
    stopLingerPoll();
    if (w.pop.clickTimer) w.pop.clickTimer->stop();
    w.pop.pendingAction.clear();
    // A chat already on screen takes the same animated swap the right-click route does, but is
    // never adopted as a peek.
    if (act == w.acts.chat && w.acts.chat->isChecked()) {
      w.pop.anchor.clear();
      w.pop.peekAction.clear();
      w.openChatCompact(btn);
      return;
    }
    w.pop.anchor = btn;
    w.pop.peekAction = act;
    act->trigger();
    // exec() blocks until the dialog closes, so reaching here ends the peek; only the non-blocking
    // chat keeps its flag until Alt release.
    if (act != w.acts.chat) w.pop.peekAction.clear();
  }

  // exec() inside the Enter left Qt's last mouse receiver stale for the whole peek, so every
  // move re-entered widgets; a tick later the hover record is current.
  void PopoverGestures::altPeekOpenSoon(QToolButton* btn, QAction* act) {
    QTimer::singleShot(0, &w, [this, b = QPointer<QToolButton>(btn), a = QPointer<QAction>(act)] {
      if (b && a && support::altKeyHeld()) altPeekOpen(b, a);
    });
  }

  // A docked panel or a user-chosen float is never touched.
  void PopoverGestures::closeCompactChatFor(QWidget* opener) {
    if (!w.acts.chat || !w.acts.chat->isChecked() || !w.chatCompactShowing()) return;
    if (opener && w.chatDock && w.chatDock->isAncestorOf(opener)) return;
    w.pop.peekAction.clear();
    w.acts.chat->setChecked(false);
  }

  // Linger poll: close once the pointer is outside, unless a field or the composer holds typed
  // content.
  void PopoverGestures::startLingerPoll() {
    if (!w.pop.lingerPoll) {
      w.pop.lingerPoll = new QTimer(&w);
      w.pop.lingerPoll->setInterval(120);
      QObject::connect(w.pop.lingerPoll, &QTimer::timeout, &w, [this] {
        QWidget* surface = w.pop.active
            ? static_cast<QWidget*>(w.pop.active.data())
            : ((w.chatDock && w.chatDock->isFloating() && w.chatDock->isVisible()) ? w.chatDock : nullptr);
        if (!surface) { w.pop.lingerPoll->stop(); return; }
        const QRect box = w.pop.active ? popoverRectGlobal() : surface->frameGeometry();
        if (box.contains(QCursor::pos()) || support::pointerInPopupOf(surface)) return;
        if (hasTypedContentInside(surface)) return;
        if (surface == w.chatDock && w.chatDock->hasComposerText()) return;
        w.pop.lingerPoll->stop();
        if (w.pop.active) w.dismissPopover();
        else if (w.acts.chat && w.acts.chat->isChecked()) w.acts.chat->setChecked(false);
      });
    }
    w.pop.lingerPoll->start();
  }

  void PopoverGestures::stopLingerPoll() { if (w.pop.lingerPoll) w.pop.lingerPoll->stop(); }

  QRect PopoverGestures::popoverRectGlobal() const {
    if (w.pop.overlay)
      return QRect(w.pop.overlay->mapToGlobal(QPoint(0, 0)), w.pop.overlay->size());
    return w.pop.active ? w.pop.active->frameGeometry() : QRect();
  }

  bool PopoverGestures::handlePopoverPress(QWidget* target, const QPoint& globalPos,
                                      Qt::MouseButton button) {
    if (!w.pop.active) return false;
    PopoverHost::PressFacts f;
    // The popover lives inside this window, so this test tells inside from outside.
    f.insidePopover = popoverRectGlobal().contains(globalPos);
    if (target) {
      // A press in a nested dialog (a confirm, a native picker) is left alone.
      f.otherWindowOwns = target->window() != &w;
    } else {
      // Polled press: any other visible top-level owns that click.
      for (QWidget* top : QApplication::topLevelWidgets())
        if (top != &w && top != w.pop.active.data() && top->isVisible() &&
            top->frameGeometry().contains(globalPos))
          f.otherWindowOwns = true;
    }
    f.onLogo =
        w.tools.logoBtn && (target ? target == w.tools.logoBtn
                            : QRect(w.tools.logoBtn->mapToGlobal(QPoint(0, 0)), w.tools.logoBtn->size())
                                  .contains(globalPos));
    f.accentPopover = w.pop.active->objectName() == QLatin1String("accentPopover");
    f.peekingAccent = w.pop.peekAction.data() == w.acts.accent;
    // The press travels on, but must not re-open what it just dismissed — the popover is non-
    // modal, so it reaches the button.
    f.onPopoverIcon = target && (target == w.tools.logoBtn || w.pop.buttons.contains(target));

    const auto verdict = PopoverHost::judgePress(f, button);
    if (verdict.clearPeek) w.pop.peekAction.clear();
    // Side by side, a click on the popover's own icon grows it into the full window.
    if (verdict.dismiss && support::multiWindow() && button == Qt::LeftButton && target && target == w.pop.openAnchor)
      w.pop.fullNextAction = w.pop.buttons.value(target, nullptr);
    if (verdict.dismiss) w.dismissPopover();
    if (verdict.armDismissClick) w.pop.dismissClick = true;
    return verdict.consume;
  }
}  // namespace stencil::gui
