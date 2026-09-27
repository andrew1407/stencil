// eventFilter chain, the popover gestures. Order and verdicts: MainWindowEvents.cpp.
#include "MainWindow.hpp"
#include "WindowEvents.hpp"
#include "mainWindowHelpers.hpp"   // hasTypedContentInside
#include "comboAltPeek.hpp"   // a selector list the popover opened is still the popover
#include "ChatDock.hpp"
#include <QComboBox>
#include <QDialog>
#include <QPushButton>

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

    // The accent popover's colour row under the pointer now — geometry, not a hover record the
    // preview's theme swap can leave stale.
    QPushButton* accentRowAt(QDialog* box, const QPoint& at) {
      if (!box || box->objectName() != QLatin1String("accentPopover")) return nullptr;
      for (QPushButton* row : box->findChildren<QPushButton*>())
        if (row->isVisible() && !row->property("accentKey").toString().isEmpty() &&
            QRect(row->mapToGlobal(QPoint(0, 0)), row->size()).contains(at))
          return row;
      return nullptr;
    }
  }  // namespace

  std::optional<bool> WindowEvents::filterPopoverGestures(QObject* obj, QEvent* event) {
    // The pointer on the open popover's box, or on a list one of its selectors opened.
    const auto pointerOnPopover = [this] {
      return w.parts.popoverGestures.popoverRectGlobal().contains(QCursor::pos()) || support::pointerInPopupOf(w.pop.overlay);
    };
    // A press back on this window dismisses the popover (its exec() is modal, so the press would be discarded);
    // a press in a NESTED dialog belongs to another window and is left alone.
    if (w.pop.active && event->type() == QEvent::MouseButtonPress) {
      if (auto* widget = qobject_cast<QWidget*>(obj)) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (w.parts.popoverGestures.handlePopoverPress(widget, me->globalPosition().toPoint(), me->button()))
          return true;   // a consumed logo gesture goes no further
      }
    }
    if (w.chatCompactShowing() && event->type() == QEvent::MouseButtonPress) {
      auto* widget = qobject_cast<QWidget*>(obj);
      // The chat icon keeps its own gestures — dismissing on ITS press would turn the toggle into a reopen.
      const bool onChatBtn = widget && w.pop.buttons.value(widget, nullptr) == w.acts.chat;
      if (widget && widget->window() == &w && !onChatBtn && w.acts.chat) w.acts.chat->setChecked(false);
    }
    // The RELEASE is swallowed and replaced with a deferred trigger (exec() would block before a dblclick arrived).
    // Skipped while a QMenu popup is open: it owns Alt itself, and the cursor check would open a popover on top of it.
    const bool aMenuPopupIsOpen = qobject_cast<QMenu*>(QApplication::activePopupWidget()) != nullptr;
    if (!aMenuPopupIsOpen && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(event)->isAutoRepeat() && !typingFocus() &&
        !(w.pop.altPress == event && w.pop.altPressAt == static_cast<QKeyEvent*>(event)->timestamp())) {
      w.pop.altPress = event;
      w.pop.altPressAt = static_cast<QKeyEvent*>(event)->timestamp();
      // Export-options popups (browser export/optionsMenu.js altHover) are plain QMenus, so there is no exec()/pop.active
      // to fold them into. Checked FIRST and exclusive per keypress: one Alt hover opens at most one thing.
      bool spent = false;   // this press opened something
      if (!w.pop.active && !w.pop.peekExportMenu) {
        auto tryOpen = [this](QAction* act, QMenu* menu) {
          if (!act || !menu || !act->isEnabled()) return false;
          QWidget* btn = w.buttonForAction(act);
          if (!btn || !btn->isVisible()) return false;
          if (!(btn->underMouse() || btn->rect().contains(btn->mapFromGlobal(QCursor::pos()))))
            return false;
          w.pop.peekExportMenu = menu;
          menu->popup(btn->mapToGlobal(QPoint(0, btn->height())));
          return true;
        };
        spent =
            tryOpen(w.acts.copyImage, w.acts.copyImageOptionsMenu) || tryOpen(w.acts.saveImage, w.acts.saveImageOptionsMenu);
      }
      // Resting ON an open popover is not resting on the icons its box covers (same guard as the glide poll).
      const bool onOpenBox = w.pop.active && pointerOnPopover();
      if (!spent) {
        for (auto it = w.pop.buttons.cbegin(); it != w.pop.buttons.cend(); ++it) {
          auto* btn = static_cast<QToolButton*>(it.key());
          if (!it.value()->isEnabled()) continue;   // a disabled icon opens nothing
          // underMouse() is what the offscreen GUI test can mock, but it is blind to an icon the open box COVERS — hence onOpenBox.
          if (btn->isVisible() && (btn->underMouse() ||
                                   (!onOpenBox &&
                                    btn->rect().contains(btn->mapFromGlobal(QCursor::pos()))))) {
            if (w.pop.active && btn == w.pop.openAnchor) break;
            if (w.pop.active) {
              // A popover already shows: the reject unwinds exec(), and execMaybePopover opens the next.
              w.pop.peekNextButton = btn;
              w.pop.peekNextAction = it.value();
              w.dismissPopover();
            } else {
              w.parts.popoverGestures.altPeekOpen(btn, it.value());
            }
            spent = true;
            break;
          }
        }
      }
      // Spent: the peek's exec() spans the Alt RELEASE, which clears altPress, so the press
      // climbing on to the next parent would read as a fresh one and reopen what just closed.
      if (spent) {
        event->accept();
        return true;
      }
    }
    // Releasing Alt ends the peek; an ENGAGED peek (cursor inside, or typed content) LINGERS via startLingerPoll.
    if (event->type() == QEvent::KeyRelease &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(event)->isAutoRepeat()) {
      w.pop.altPress = nullptr;
      if (QAction* act = w.pop.peekAction.data()) {
        w.pop.peekAction.clear();
        // A colour peek released on a row picks it and lingers: the pointer is still inside.
        if (QPushButton* row = act == w.acts.accent ? accentRowAt(w.pop.active, QCursor::pos()) : nullptr) {
          w.parts.theme.commitAccent(row->property("accentKey").toString());
          w.parts.popoverGestures.startLingerPoll();
        } else if (w.pop.active) {
          if (pointerOnPopover() || hasTypedContentInside(w.pop.active))
            w.parts.popoverGestures.startLingerPoll();
          else
            w.dismissPopover();
        } else if (act == w.acts.chat && w.acts.chat->isChecked()) {
          const bool engaged = w.chatDock && w.chatDock->isFloating() &&
              (w.chatDock->frameGeometry().contains(QCursor::pos()) || w.chatDock->hasComposerText());
          if (engaged) w.parts.popoverGestures.startLingerPoll();
          else w.acts.chat->setChecked(false);
        }
      }
      // Same rule for an export-options popup: a plain QMenu closes itself on any outside click, so no lingerPoll.
      if (QMenu* menu = w.pop.peekExportMenu.data()) {
        w.pop.peekExportMenu.clear();
        if (!menu->geometry().contains(QCursor::pos())) menu->close();
      }
    }
    // Losing the keyboard (Cmd-Tab eats a peek's Alt keyup) also ends the popover — except a NESTED dialog or a list
    // of its own selector took the focus for us (some platforms activate a popup), or the user has typed into the form.
    if (event->type() == QEvent::WindowDeactivate && w.pop.active && obj == &w &&
        !support::popupOf(w.pop.overlay)) {
      bool nested = false;   // a dialog the popover opened took the focus for us
      for (QWidget* widget : QApplication::topLevelWidgets())
        if (widget != &w && widget->isVisible() && widget->isWindow() && qobject_cast<QDialog*>(widget)) {
          nested = true;
          break;
        }
      if (!nested && !hasTypedContentInside(w.pop.active)) {
        w.pop.peekAction.clear();
        w.dismissPopover();
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> WindowEvents::filterPopoverButton(QObject* obj, QEvent* event) {
    // The logo is IN pop.buttons for the Alt-peek machinery but keeps its own click/dblclick gestures.
    if (QAction* act = w.pop.buttons.value(obj, nullptr); act && obj != w.tools.logoBtn) {
      auto* btn = static_cast<QToolButton*>(obj);
      if (event->type() == QEvent::Enter && support::altKeyHeld()) {
        w.parts.popoverGestures.altPeekOpenSoon(btn, act);   // the mouse route always peeks
        return false;   // hover styling must still see the Enter
      }
      if (event->type() == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        w.pop.swallowRelease = false;   // a fresh press always starts clean
      }
      if (event->type() == QEvent::MouseButtonDblClick &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        w.pop.clickTimer->stop();
        w.pop.pendingAction.clear();
        // A DISABLED icon opens nothing; swallow without arming, or a stale pop.anchor pins the NEXT dialog here.
        if (!act->isEnabled()) { btn->setDown(false); return true; }
        w.pop.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
        w.parts.popoverGestures.stopLingerPoll();         // a lingering window's poll must not close THIS open
        btn->setDown(false);
        w.pop.anchor = btn;
        // The dblclick's trailing release must not re-arm the deferred click (it would toggle a non-modal target back off).
        // Set BEFORE trigger(): a modal dialog blocks in exec() and eats the release itself.
        w.pop.swallowRelease = true;
        act->trigger();
        return true;
      }
      if (event->type() == QEvent::MouseButtonRelease &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        const bool inside = btn->rect().contains(
            static_cast<QMouseEvent*>(event)->position().toPoint());
        btn->setDown(false);   // we consume the release, so un-sink the button ourselves
        if (w.pop.dismissClick) {
          w.pop.dismissClick = false;   // this click closed a popover; that was its job
        } else if (w.pop.swallowRelease) {
          w.pop.swallowRelease = false;
        } else if (inside && act->isEnabled()) {
          w.pop.pendingAction = act;
          w.pop.clickTimer->start();
        }
        return true;
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

}  // namespace stencil::gui
