// eventFilter chain, the popover gestures: the outside press that dismisses one, the
// Alt hold-to-peek and its release, a window deactivation, and the popover buttons' own
// press/dblclick/release contract. Order and verdicts: mainWindowEvents.cpp.
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"   // typedContentInside
#include "chatDock.hpp"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <optional>

namespace stencil::gui {

  // Whether keyboard focus sits in a text-entry control (spin boxes and editable
  // combos are their line edit's FOCUS PROXY, so the container is what reports).
  static bool typingFocus() {
    QWidget* f = QApplication::focusWidget();
    if (!f) return false;
    if (qobject_cast<QLineEdit*>(f) || qobject_cast<QTextEdit*>(f) ||
        qobject_cast<QPlainTextEdit*>(f) || qobject_cast<QAbstractSpinBox*>(f))
      return true;
    auto* combo = qobject_cast<QComboBox*>(f);
    return combo && combo->isEditable();
  }

  std::optional<bool> MainWindow::filterPopoverGestures(QObject* obj, QEvent* event) {
    // A popover dialog closes on a click OUTSIDE it — i.e. any press landing back on this
    // window (its exec() is modal, so that press would otherwise be silently discarded).
    // A press in a NESTED dialog (a confirm, a native picker) belongs to another window
    // and is left alone, so flows launched from inside the popover keep working.
    if (pop_.active && event->type() == QEvent::MouseButtonPress) {
      if (auto* w = qobject_cast<QWidget*>(obj)) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (handlePopoverPress(w, me->globalPosition().toPoint(), me->button()))
          return true;   // a consumed logo gesture goes no further
      }
    }
    if (chatCompactShowing() && event->type() == QEvent::MouseButtonPress) {
      auto* w = qobject_cast<QWidget*>(obj);
      // The chat icon keeps its own gestures (click toggles, dblclick reopens
      // compact) — dismissing on ITS press would turn the toggle into a reopen.
      const bool onChatBtn = w && pop_.buttons.value(w, nullptr) == actChat_;
      if (w && w->window() == this && !onChatBtn && actChat_) actChat_->setChecked(false);
    }
    // Popover buttons: the RELEASE is swallowed and replaced with the deferred
    // trigger (exec() would block before a dblclick arrived); dblclick pre-empts
    // it. Alt+hover peeks the same popover; the peek lives only while Alt is down
    // (pop_.peekAction names it for the KeyRelease). Skipped while any QMenu popup is
    // open: it owns Alt itself (row export previews), and the cursor screen-position
    // check below would otherwise "see" a toolbar icon under the popup and open its
    // popover on top, stealing the grab and closing the menu.
    const bool aMenuPopupIsOpen = qobject_cast<QMenu*>(QApplication::activePopupWidget()) != nullptr;
    if (!aMenuPopupIsOpen && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(event)->isAutoRepeat() && !typingFocus()) {
      // The copy/download-image toolbar buttons' own export-options popups
      // (browser parity: exportOptionsMenu.js altHover) — Alt+hover opens the
      // SAME popup right-click/dblclick already do (wireExportOptionsPopups).
      // Deliberately independent of pop_.buttons below: these are plain
      // QMenus opened via QMenu::popup(), not a QAction triggering a QDialog,
      // so there is no exec()/pop_.active to fold this into. Checked FIRST
      // and, on a match, skips that loop entirely for this keypress — one Alt
      // hover opens at most one thing, never a peek AND an export popup both.
      // Once open, a row's own Alt-hover preview (exportPreview.hpp) behaves
      // exactly as it does for any other opening of the same menu.
      bool openedExportMenu = false;
      if (!pop_.active && !pop_.peekExportMenu) {
        auto tryOpen = [this](QAction* act, QMenu* menu) {
          if (!act || !menu || !act->isEnabled()) return false;
          QWidget* btn = buttonForAction(act);
          if (!btn || !btn->isVisible()) return false;
          if (!(btn->underMouse() || btn->rect().contains(btn->mapFromGlobal(QCursor::pos()))))
            return false;
          pop_.peekExportMenu = menu;
          menu->popup(btn->mapToGlobal(QPoint(0, btn->height())));
          return true;
        };
        openedExportMenu =
            tryOpen(actCopyImage_, copyImageOptionsMenu_) || tryOpen(actSaveImage_, saveImageOptionsMenu_);
      }
      // Resting ON an open popover is not resting on the icons its box covers — see the
      // cursor-rect fallback below (the same guard the glide poll in execMaybePopover has).
      const bool onOpenBox = pop_.active && popoverRectGlobal().contains(QCursor::pos());
      if (!openedExportMenu) {
        for (auto it = pop_.buttons.cbegin(); it != pop_.buttons.cend(); ++it) {
          auto* btn = static_cast<QToolButton*>(it.key());
          if (!it.value()->isEnabled()) continue;   // a disabled icon opens nothing
          // underMouse() backs up the cursor-position check: same answer for a real
          // resting pointer, and it is the state the offscreen GUI test can mock — but it
          // is pure geometry, blind to an icon the open box COVERS, hence onOpenBox.
          if (btn->isVisible() && (btn->underMouse() ||
                                   (!onOpenBox &&
                                    btn->rect().contains(btn->mapFromGlobal(QCursor::pos()))))) {
            if (pop_.active) {
              // A popover (peek or sticky) already shows: switch to this icon —
              // the reject unwinds exec(), and execMaybePopover opens the next.
              pop_.peekNextButton = btn;
              pop_.peekNextAction = it.value();
              dismissPopover();
            } else {
              altPeekOpen(btn, it.value());
            }
            break;
          }
        }
      }
    }
    // Releasing Alt ends the peek (sticky dblclick/right-click opens cleared
    // pop_.peekAction and are never touched). An ENGAGED peek — cursor inside, or
    // typed content — LINGERS via startLingerPoll instead of closing under you.
    if (event->type() == QEvent::KeyRelease &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(event)->isAutoRepeat()) {
      if (QAction* act = pop_.peekAction.data()) {
        pop_.peekAction.clear();
        if (pop_.active) {
          if (popoverRectGlobal().contains(QCursor::pos()) ||
              typedContentInside(pop_.active))
            startLingerPoll();
          else
            dismissPopover();
        } else if (act == actChat_ && actChat_->isChecked()) {
          const bool engaged = chatDock_ && chatDock_->isFloating() &&
              (chatDock_->frameGeometry().contains(QCursor::pos()) || chatDock_->hasComposerText());
          if (engaged) startLingerPoll();
          else actChat_->setChecked(false);
        }
      }
      // Same hold-to-peek rule for an export-options popup opened above: releasing
      // Alt closes it UNLESS the cursor has since moved inside (engaged) — a plain
      // QMenu needs no lingerPoll of its own, since it already closes itself on any
      // outside click or Escape from here on.
      if (QMenu* menu = pop_.peekExportMenu.data()) {
        pop_.peekExportMenu.clear();
        if (!menu->geometry().contains(QCursor::pos())) menu->close();
      }
    }
    // A popover also dies when THIS WINDOW loses the keyboard — the user switched apps
    // (Cmd-Tab, which eats a peek's Alt keyup). The popover is inside this window now,
    // so it is this window's deactivation that matters; a click elsewhere in the window
    // is the press rule's job, not this one. Two exemptions, both already the house
    // rules here: a NESTED dialog the popover opened took the focus FOR us, and a form
    // the user has typed into is never yanked away (the linger rule's protection).
    if (event->type() == QEvent::WindowDeactivate && pop_.active && obj == this) {
      bool nested = false;   // a dialog the popover opened took the focus for us
      for (QWidget* w : QApplication::topLevelWidgets())
        if (w != this && w->isVisible() && w->isWindow() && qobject_cast<QDialog*>(w)) {
          nested = true;
          break;
        }
      if (!nested && !typedContentInside(pop_.active)) {
        pop_.peekAction.clear();
        dismissPopover();
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> MainWindow::filterPopoverButton(QObject* obj, QEvent* event) {
    // The logo is IN pop_.buttons (for the Alt-peek machinery) but keeps its own
    // click/dblclick gestures — the shared popover-button press handling below must
    // not hijack them, hence the exclusion.
    if (QAction* act = pop_.buttons.value(obj, nullptr); act && obj != logoBtn_) {
      auto* btn = static_cast<QToolButton*>(obj);
      if (event->type() == QEvent::Enter &&
          QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier)) {
        // The mouse route always peeks (a glide is deliberate); only the Alt
        // KEY-press above defers to a focused text control.
        altPeekOpen(btn, act);
        return false;   // hover styling must still see the Enter
      }
      if (event->type() == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        pop_.swallowRelease = false;   // a fresh press always starts clean
      }
      if (event->type() == QEvent::MouseButtonDblClick &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        pop_.clickTimer->stop();
        pop_.pendingAction.clear();
        // A DISABLED icon opens nothing — mini window included. Swallow without
        // arming: a stale pop_.anchor would pin the NEXT dialog to this icon.
        if (!act->isEnabled()) { btn->setDown(false); return true; }
        pop_.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
        stopLingerPoll();         // a lingering window's poll must not close THIS open
        btn->setDown(false);
        pop_.anchor = btn;
        // The dblclick's own trailing release must not re-arm the deferred
        // click below — for a NON-modal target (the chat dock) that deferred
        // trigger would toggle it straight back off. Set BEFORE trigger():
        // a modal dialog blocks in exec() and eats the release itself.
        pop_.swallowRelease = true;
        act->trigger();
        return true;
      }
      if (event->type() == QEvent::MouseButtonRelease &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        const bool inside = btn->rect().contains(
            static_cast<QMouseEvent*>(event)->position().toPoint());
        btn->setDown(false);   // we consume the release, so un-sink the button ourselves
        if (pop_.dismissClick) {
          pop_.dismissClick = false;   // this click closed a popover; that was its job
        } else if (pop_.swallowRelease) {
          pop_.swallowRelease = false;
        } else if (inside && act->isEnabled()) {
          pop_.pendingAction = act;
          pop_.clickTimer->start();
        }
        return true;
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

}  // namespace stencil::gui
