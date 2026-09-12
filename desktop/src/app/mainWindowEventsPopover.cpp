// eventFilter chain, the popover gestures. Order and verdicts: mainWindowEvents.cpp.
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"   // hasTypedContentInside
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

  namespace {
    // Spin boxes and editable combos are their line edit's FOCUS PROXY, so the container reports.
    bool typingFocus() {
      QWidget* f = QApplication::focusWidget();
      if (!f) return false;
      if (qobject_cast<QLineEdit*>(f) || qobject_cast<QTextEdit*>(f) ||
          qobject_cast<QPlainTextEdit*>(f) || qobject_cast<QAbstractSpinBox*>(f))
        return true;
      auto* combo = qobject_cast<QComboBox*>(f);
      return combo && combo->isEditable();
    }
  }  // namespace

  std::optional<bool> MainWindow::filterPopoverGestures(QObject* obj, QEvent* event) {
    // A press back on this window dismisses the popover (its exec() is modal, so the press would be discarded);
    // a press in a NESTED dialog belongs to another window and is left alone.
    if (pop_.active && event->type() == QEvent::MouseButtonPress) {
      if (auto* w = qobject_cast<QWidget*>(obj)) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (handlePopoverPress(w, me->globalPosition().toPoint(), me->button()))
          return true;   // a consumed logo gesture goes no further
      }
    }
    if (chatCompactShowing() && event->type() == QEvent::MouseButtonPress) {
      auto* w = qobject_cast<QWidget*>(obj);
      // The chat icon keeps its own gestures — dismissing on ITS press would turn the toggle into a reopen.
      const bool onChatBtn = w && pop_.buttons.value(w, nullptr) == actChat_;
      if (w && w->window() == this && !onChatBtn && actChat_) actChat_->setChecked(false);
    }
    // The RELEASE is swallowed and replaced with a deferred trigger (exec() would block before a dblclick arrived).
    // Skipped while a QMenu popup is open: it owns Alt itself, and the cursor check would open a popover on top of it.
    const bool aMenuPopupIsOpen = qobject_cast<QMenu*>(QApplication::activePopupWidget()) != nullptr;
    if (!aMenuPopupIsOpen && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(event)->isAutoRepeat() && !typingFocus()) {
      // Export-options popups (browser exportOptionsMenu.js altHover) are plain QMenus, so there is no exec()/pop_.active
      // to fold them into. Checked FIRST and exclusive per keypress: one Alt hover opens at most one thing.
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
      // Resting ON an open popover is not resting on the icons its box covers (same guard as the glide poll).
      const bool onOpenBox = pop_.active && popoverRectGlobal().contains(QCursor::pos());
      if (!openedExportMenu) {
        for (auto it = pop_.buttons.cbegin(); it != pop_.buttons.cend(); ++it) {
          auto* btn = static_cast<QToolButton*>(it.key());
          if (!it.value()->isEnabled()) continue;   // a disabled icon opens nothing
          // underMouse() is what the offscreen GUI test can mock, but it is blind to an icon the open box COVERS — hence onOpenBox.
          if (btn->isVisible() && (btn->underMouse() ||
                                   (!onOpenBox &&
                                    btn->rect().contains(btn->mapFromGlobal(QCursor::pos()))))) {
            if (pop_.active) {
              // A popover already shows: the reject unwinds exec(), and execMaybePopover opens the next.
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
    // Releasing Alt ends the peek; an ENGAGED peek (cursor inside, or typed content) LINGERS via startLingerPoll.
    if (event->type() == QEvent::KeyRelease &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(event)->isAutoRepeat()) {
      if (QAction* act = pop_.peekAction.data()) {
        pop_.peekAction.clear();
        if (pop_.active) {
          if (popoverRectGlobal().contains(QCursor::pos()) ||
              hasTypedContentInside(pop_.active))
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
      // Same rule for an export-options popup: a plain QMenu closes itself on any outside click, so no lingerPoll.
      if (QMenu* menu = pop_.peekExportMenu.data()) {
        pop_.peekExportMenu.clear();
        if (!menu->geometry().contains(QCursor::pos())) menu->close();
      }
    }
    // Losing the keyboard (Cmd-Tab eats a peek's Alt keyup) also ends the popover — except a NESTED dialog took the
    // focus for us, or the user has typed into the form.
    if (event->type() == QEvent::WindowDeactivate && pop_.active && obj == this) {
      bool nested = false;   // a dialog the popover opened took the focus for us
      for (QWidget* w : QApplication::topLevelWidgets())
        if (w != this && w->isVisible() && w->isWindow() && qobject_cast<QDialog*>(w)) {
          nested = true;
          break;
        }
      if (!nested && !hasTypedContentInside(pop_.active)) {
        pop_.peekAction.clear();
        dismissPopover();
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> MainWindow::filterPopoverButton(QObject* obj, QEvent* event) {
    // The logo is IN pop_.buttons for the Alt-peek machinery but keeps its own click/dblclick gestures.
    if (QAction* act = pop_.buttons.value(obj, nullptr); act && obj != logoBtn_) {
      auto* btn = static_cast<QToolButton*>(obj);
      if (event->type() == QEvent::Enter &&
          QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier)) {
        // The mouse route always peeks; only the Alt KEY-press above defers to a focused text control.
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
        // A DISABLED icon opens nothing; swallow without arming, or a stale pop_.anchor pins the NEXT dialog here.
        if (!act->isEnabled()) { btn->setDown(false); return true; }
        pop_.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
        stopLingerPoll();         // a lingering window's poll must not close THIS open
        btn->setDown(false);
        pop_.anchor = btn;
        // The dblclick's trailing release must not re-arm the deferred click (it would toggle a non-modal target back off).
        // Set BEFORE trigger(): a modal dialog blocks in exec() and eats the release itself.
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
