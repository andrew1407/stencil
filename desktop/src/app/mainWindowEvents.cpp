#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "canvasTooltip.hpp"
#include "chatDock.hpp"
#include "chatMenuPanel.hpp"
#include "../support/dockGrip.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"
#include "notifications.hpp"
#include "selectionPanel.hpp"
#include "theme.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>

// MainWindow::eventFilter — deliberately ONE function (the app-wide event
// routing reads top to bottom) — plus its two file-local focus helpers.
// Split from mainWindow.cpp; same class, definitions only.

namespace stencil::gui {

  // True when `obj` is a widget that takes typed text — the chat box, a name field, any
  // line edit in a dialog.
  static bool isTextEntry(QObject* obj) {
    return qobject_cast<QLineEdit*>(obj) || qobject_cast<QPlainTextEdit*>(obj) ||
           qobject_cast<QTextEdit*>(obj);
  }

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

  bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // Canvas scrollbar auto-hide: hovering a bar directly (to find/grab it) must never let
    // it fade out from under the cursor — see revealCanvasScrollbars/scheduleScrollbarHide.
    if (scroll_ && (obj == canvasScrollBar(Qt::Horizontal) || obj == canvasScrollBar(Qt::Vertical))) {
      if (event->type() == QEvent::Enter) { scrollbarHovered_ = true; revealCanvasScrollbars(); }
      else if (event->type() == QEvent::Leave) { scrollbarHovered_ = false; scheduleScrollbarHide(); }
    }
    // Docked-chat resize moves the toast stack out of its way (never consumed).
    if (obj == chatDock_ && event->type() == QEvent::Resize) syncToastInset();
    // Disabled controls show `not-allowed` (browser rule) via an override cursor:
    // Qt never sends a disabled widget the move (only app-wide filters see it),
    // and a row-level cursor sticks to the whole row — so this is the one way.
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter ||
        event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverMove) {
      auto* w = qobject_cast<QWidget*>(obj);
      QWidget* row = nullptr;         // the toolbar row this event is about, if any
      bool overDead = false;
      if (w && w->parentWidget() && w->parentWidget()->property("toolRow").toBool()) {
        row = w->parentWidget();                                     // the discarded-move path
        overDead = !w->isEnabled();
      } else if (w && w->property("toolRow").toBool() && event->type() == QEvent::MouseMove) {
        // …and the row itself, for the gaps between its controls: hit-tested by hand, since
        // childAt() skips the very widget the mouse cannot reach.
        row = w;
        const QPoint p = static_cast<QMouseEvent*>(event)->position().toPoint();
        for (QObject* c : w->children()) {
          auto* cw = qobject_cast<QWidget*>(c);
          if (!cw || !cw->isVisible() || !cw->geometry().contains(p)) continue;
          overDead = !cw->isEnabled();
          break;
        }
      }
      if (row) {
        setBlockedCursor(overDead);
        blockedRow_ = overDead ? row : nullptr;
      } else if (event->type() == QEvent::MouseMove && blockedCursorOn_ && w &&
                 !(blockedRow_ && w->isAncestorOf(blockedRow_))) {
        // A move genuinely somewhere else undresses the pointer. One physical move is
        // delivered several times over — to the control, then bubbling up through the row's
        // toolbar and window — and treating those ANCESTOR copies as "somewhere else" made
        // the cursor flip off and on again within a single move.
        setBlockedCursor(false);
        blockedRow_ = nullptr;
      }
    }
    // The pointer can leave the window without a move landing anywhere else.
    if (event->type() == QEvent::Leave || event->type() == QEvent::WindowDeactivate ||
        (obj == this && (event->type() == QEvent::Hide || event->type() == QEvent::Close)))
      setBlockedCursor(false);
    // The grip must FOLLOW the panel through every geometry change — a separator
    // drag, a dock split with the chat panel, a float/redock — not only the canvas
    // viewport's resizes: anchored to a stale panel rect it ends up painting its bar
    // stranded INSIDE the widened panel.
    if (obj == selPanel_ && panelGrip_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Show || t == QEvent::Hide)
        positionPanelGrip();
    }
    // …and the chat dock's resize edge follows ITS panel exactly the same way.
    if (obj == chatDock_ && chatEdge_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Show || t == QEvent::Hide)
        positionChatEdge();
    }
    // The canvas↔panel separator grip (support/dockGrip.hpp): the separator belongs to
    // the QMainWindow itself, so ITS hover and drag arrive here — the grip lights and
    // grows while the cursor is on the strip, and stays hot through a drag (the browser
    // .panel-resizer keeps its accent while `.dragging`).
    if (obj == this && panelGrip_ && panelGrip_->isVisible()) {
      const QEvent::Type t = event->type();
      if (t == QEvent::HoverEnter || t == QEvent::HoverMove) {
        const QPoint p = static_cast<QHoverEvent*>(event)->position().toPoint();
        panelGrip_->setHot(panelGripDrag_ || panelGrip_->geometry().contains(p));
      } else if (t == QEvent::MouseButtonPress &&
                 static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        if (panelGrip_->geometry().contains(
                static_cast<QMouseEvent*>(event)->position().toPoint())) {
          panelGripDrag_ = true;
          panelGrip_->setHot(true);
        }
      } else if (t == QEvent::MouseButtonRelease && panelGripDrag_) {
        panelGripDrag_ = false;
        panelGrip_->setHot(panelGrip_->geometry().contains(
            static_cast<QMouseEvent*>(event)->position().toPoint()));
      } else if (t == QEvent::HoverLeave || t == QEvent::Leave) {
        if (!panelGripDrag_) panelGrip_->setHot(false);
      }
    }
    // The chat dock's resize edge (browser .chat-resizer): tints on hover and for the
    // whole drag, like the grip above. Hit-tested against the separator's own rect, not
    // the thicker painted band — it must not light where Qt starts no resize.
    if (obj == this && chatEdge_ && chatEdge_->isVisible()) {
      const QEvent::Type t = event->type();
      if (t == QEvent::HoverEnter || t == QEvent::HoverMove) {
        const QPoint p = static_cast<QHoverEvent*>(event)->position().toPoint();
        chatEdge_->setHot(chatEdgeDrag_ || chatEdgeHit_.contains(p));
      } else if (t == QEvent::MouseButtonPress &&
                 static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        if (chatEdgeHit_.contains(static_cast<QMouseEvent*>(event)->position().toPoint())) {
          chatEdgeDrag_ = true;
          chatEdge_->setHot(true);
        }
      } else if (t == QEvent::MouseButtonRelease && chatEdgeDrag_) {
        chatEdgeDrag_ = false;
        chatEdge_->setHot(
            chatEdgeHit_.contains(static_cast<QMouseEvent*>(event)->position().toPoint()));
      } else if (t == QEvent::HoverLeave || t == QEvent::Leave) {
        if (!chatEdgeDrag_) chatEdge_->setHot(false);
      }
    }
    // A lost-focus window never delivers the held arrows' key-up (browser parity:
    // controlsBinder.js's own blur listener clears #arrowsHeld the same way).
    if (obj == this && event->type() == QEvent::WindowDeactivate) {
      panLeftHeld_ = panRightHeld_ = panUpHeld_ = panDownHeld_ = panShiftHeld_ = false;
      if (arrowPanTimer_) arrowPanTimer_->stop();
    }
    // While a TEXT BOX has focus, the standard editing chords belong to it — not to a canvas
    // shortcut that happens to share the chord. ⌥⌫ (deleteLine) is the one that bit: typing in
    // the chat, it deleted the selected LINE instead of the word behind the cursor. Claiming
    // ShortcutOverride hands the key back to the widget; the action still works everywhere else.
    if (event->type() == QEvent::ShortcutOverride && isTextEntry(obj)) {
      auto* ke = static_cast<QKeyEvent*>(event);
      static const QKeySequence::StandardKey kEditing[] = {
          QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord,
          QKeySequence::DeleteCompleteLine, QKeySequence::MoveToPreviousWord,
          QKeySequence::MoveToNextWord,     QKeySequence::SelectPreviousWord,
          QKeySequence::SelectNextWord,     QKeySequence::MoveToStartOfLine,
          QKeySequence::MoveToEndOfLine,    QKeySequence::SelectStartOfLine,
          QKeySequence::SelectEndOfLine,    QKeySequence::Undo,
          QKeySequence::Redo,               QKeySequence::SelectAll};
      for (const auto key : kEditing) {
        if (ke->matches(key)) {
          event->accept();
          return true;
        }
      }
    }
    // Escape, app-wide (KeyPress AND ShortcutOverride, so nothing swallows it):
    // closes an open popover first (the compact chat follows the same mini-window
    // contract — docked/user-adopted floats never dismiss this way), then leaves
    // fullscreen (gated on fsActive_ — isFullScreen() is unreliable on macOS).
    if (activePopover_ &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      auto* w = qobject_cast<QWidget*>(obj);
      if (w && (w->window() == this || w == activePopover_.data() ||
                activePopover_->isAncestorOf(w))) {
        altPeekAction_.clear();   // Escape is deliberate: it ends a peek for good
        dismissPopover();
        return true;
      }
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && chatCompactShowing()) {
      if (event->type() == QEvent::KeyPress && actChat_) actChat_->setChecked(false);
      return true;   // the ShortcutOverride claim keeps focused widgets from eating it
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && fsActive_) {
      toggleFullscreen();
      return true;
    }
    // A popover dialog closes on a click OUTSIDE it — i.e. any press landing back on this
    // window (its exec() is modal, so that press would otherwise be silently discarded).
    // A press in a NESTED dialog (a confirm, a native picker) belongs to another window
    // and is left alone, so flows launched from inside the popover keep working.
    if (activePopover_ && event->type() == QEvent::MouseButtonPress) {
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
      const bool onChatBtn = w && popoverButtons_.value(w, nullptr) == actChat_;
      if (w && w->window() == this && !onChatBtn && actChat_) actChat_->setChecked(false);
    }
    // Popover buttons: the RELEASE is swallowed and replaced with the deferred
    // trigger (exec() would block before a dblclick arrived); dblclick pre-empts
    // it. Alt+hover peeks the same popover; the peek lives only while Alt is down
    // (altPeekAction_ names it for the KeyRelease). Skipped while any QMenu popup is
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
      // Deliberately independent of popoverButtons_ below: these are plain
      // QMenus opened via QMenu::popup(), not a QAction triggering a QDialog,
      // so there is no exec()/activePopover_ to fold this into. Checked FIRST
      // and, on a match, skips that loop entirely for this keypress — one Alt
      // hover opens at most one thing, never a peek AND an export popup both.
      // Once open, a row's own Alt-hover preview (exportPreview.hpp) behaves
      // exactly as it does for any other opening of the same menu.
      bool openedExportMenu = false;
      if (!activePopover_ && !altPeekExportMenu_) {
        auto tryOpen = [this](QAction* act, QMenu* menu) {
          if (!act || !menu || !act->isEnabled()) return false;
          QWidget* btn = buttonForAction(act);
          if (!btn || !btn->isVisible()) return false;
          if (!(btn->underMouse() || btn->rect().contains(btn->mapFromGlobal(QCursor::pos()))))
            return false;
          altPeekExportMenu_ = menu;
          menu->popup(btn->mapToGlobal(QPoint(0, btn->height())));
          return true;
        };
        openedExportMenu =
            tryOpen(actCopyImage_, copyImageOptionsMenu_) || tryOpen(actSaveImage_, saveImageOptionsMenu_);
      }
      // Resting ON an open popover is not resting on the icons its box covers — see the
      // cursor-rect fallback below (the same guard the glide poll in execMaybePopover has).
      const bool onOpenBox = activePopover_ && popoverRectGlobal().contains(QCursor::pos());
      if (!openedExportMenu) {
        for (auto it = popoverButtons_.cbegin(); it != popoverButtons_.cend(); ++it) {
          auto* btn = static_cast<QToolButton*>(it.key());
          if (!it.value()->isEnabled()) continue;   // a disabled icon opens nothing
          // underMouse() backs up the cursor-position check: same answer for a real
          // resting pointer, and it is the state the offscreen GUI test can mock — but it
          // is pure geometry, blind to an icon the open box COVERS, hence onOpenBox.
          if (btn->isVisible() && (btn->underMouse() ||
                                   (!onOpenBox &&
                                    btn->rect().contains(btn->mapFromGlobal(QCursor::pos()))))) {
            if (activePopover_) {
              // A popover (peek or sticky) already shows: switch to this icon —
              // the reject unwinds exec(), and execMaybePopover opens the next.
              altPeekNextButton_ = btn;
              altPeekNextAction_ = it.value();
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
    // altPeekAction_ and are never touched). An ENGAGED peek — cursor inside, or
    // typed content — LINGERS via startLingerPoll instead of closing under you.
    if (event->type() == QEvent::KeyRelease &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(event)->isAutoRepeat()) {
      if (QAction* act = altPeekAction_.data()) {
        altPeekAction_.clear();
        if (activePopover_) {
          if (popoverRectGlobal().contains(QCursor::pos()) ||
              typedContentInside(activePopover_))
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
      if (QMenu* menu = altPeekExportMenu_.data()) {
        altPeekExportMenu_.clear();
        if (!menu->geometry().contains(QCursor::pos())) menu->close();
      }
    }
    // A popover also dies when THIS WINDOW loses the keyboard — the user switched apps
    // (Cmd-Tab, which eats a peek's Alt keyup). The popover is inside this window now,
    // so it is this window's deactivation that matters; a click elsewhere in the window
    // is the press rule's job, not this one. Two exemptions, both already the house
    // rules here: a NESTED dialog the popover opened took the focus FOR us, and a form
    // the user has typed into is never yanked away (the linger rule's protection).
    if (event->type() == QEvent::WindowDeactivate && activePopover_ && obj == this) {
      bool nested = false;   // a dialog the popover opened took the focus for us
      for (QWidget* w : QApplication::topLevelWidgets())
        if (w != this && w->isVisible() && w->isWindow() && qobject_cast<QDialog*>(w)) {
          nested = true;
          break;
        }
      if (!nested && !typedContentInside(activePopover_)) {
        altPeekAction_.clear();
        dismissPopover();
      }
    }
    // The logo is IN popoverButtons_ (for the Alt-peek machinery) but keeps its own
    // click/dblclick gestures — the shared popover-button press handling below must
    // not hijack them, hence the exclusion.
    if (QAction* act = popoverButtons_.value(obj, nullptr); act && obj != logoBtn_) {
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
        popoverSwallowRelease_ = false;   // a fresh press always starts clean
      }
      if (event->type() == QEvent::MouseButtonDblClick &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        popoverClickTimer_->stop();
        popoverPendingAction_.clear();
        // A DISABLED icon opens nothing — mini window included. Swallow without
        // arming: a stale popoverAnchor_ would pin the NEXT dialog to this icon.
        if (!act->isEnabled()) { btn->setDown(false); return true; }
        altPeekAction_.clear();   // a deliberate open is sticky — Alt release keeps it
        stopLingerPoll();         // a lingering window's poll must not close THIS open
        btn->setDown(false);
        popoverAnchor_ = btn;
        // The dblclick's own trailing release must not re-arm the deferred
        // click below — for a NON-modal target (the chat dock) that deferred
        // trigger would toggle it straight back off. Set BEFORE trigger():
        // a modal dialog blocks in exec() and eats the release itself.
        popoverSwallowRelease_ = true;
        act->trigger();
        return true;
      }
      if (event->type() == QEvent::MouseButtonRelease &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        const bool inside = btn->rect().contains(
            static_cast<QMouseEvent*>(event)->position().toPoint());
        btn->setDown(false);   // we consume the release, so un-sink the button ourselves
        if (popoverDismissClick_) {
          popoverDismissClick_ = false;   // this click closed a popover; that was its job
        } else if (popoverSwallowRelease_) {
          popoverSwallowRelease_ = false;
        } else if (inside && act->isEnabled()) {
          popoverPendingAction_ = act;
          popoverClickTimer_->start();
        }
        return true;
      }
    }
    // Zoom field → open the preset list without the separate arrow. Trigger on the click's
    // mouse-RELEASE (not press/focus): showing the popup during the press cycle lets the pending
    // release land outside it and immediately dismiss it (macOS), so it just flashed. Tab/keyboard
    // focus opens it too. The field stays editable, so the user can still type over the popup.
    if (zoom_ && obj == zoom_->lineEdit()) {
      const auto openPopup = [this] {
        QTimer::singleShot(0, this, [this] {
          if (zoom_ && zoom_->lineEdit()->hasFocus() && !zoom_->view()->isVisible()) zoom_->showPopup();
        });
      };
      if (event->type() == QEvent::MouseButtonRelease &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        openPopup();
      } else if (event->type() == QEvent::FocusIn) {
        const auto reason = static_cast<QFocusEvent*>(event)->reason();
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
            reason == Qt::ShortcutFocusReason)
          openPopup();
      }
      return false;   // never consume — the field's caret / typing must behave normally
    }
    // Alt-GLIDE onto the logo (Alt already held, cursor arrives): the same peek route
    // the shared block below gives every popover icon — the logo has its own copy
    // because it is excluded from that block to protect its click/dblclick gestures.
    if (obj == logoBtn_ && event->type() == QEvent::Enter &&
        QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier)) {
      altPeekOpen(logoBtn_, actAccent_);
      return false;   // hover styling (LogoHoverFx) must still see the Enter
    }
    // Alt+left-press on the logo is consumed whole: the Alt keypress (or glide)
    // already owns the peek, and a stray Alt+click must not fall through to clicked()
    // and fire the accent CYCLE mid-Alt-gesture.
    if (obj == logoBtn_ && event->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::AltModifier)) return true;
    }
    // Logo double-click → custom theme-colour picker (browser parity). Cancels the pending single-
    // click accent-cycle first, then opens the non-native colour dialog seeded with the current accent.
    if (obj == logoBtn_ && event->type() == QEvent::MouseButtonDblClick) {
      if (logoClickTimer_) logoClickTimer_->stop();
      const QColor cur = accentPrimary(settings_.accentColor);
      const QColor c = support::pickColorAnimated(cur, this, "Theme color", logoBtn_);
      if (c.isValid()) {
        auto next = settings_;
        next.accentColor = c.name();   // store as hex → custom accent (accentPrimary handles it)
        applySettings(next, true);
      }
      return true;
    }
    // Zoom over the empty margin around a zoomed-out image (the viewport, not the
    // canvas). Mirrors CanvasWidget's Ctrl+wheel / pinch zoom; the event position is
    // already in viewport coordinates, which is what setZoomAnchored wants.
    if (scroll_ && obj == scroll_->viewport()) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize) {
        positionOverlayArrows(); positionPanelReopenButton(); positionPanelGrip();
        positionChatEdge();
      }
      // Right-click on the margin around the image opens the same context menu the
      // canvas opens — the backdrop had none. With NO image the press is left alone:
      // showContextMenu() opens nothing then (browser contextMenu.js parity), so
      // swallowing the click here would only make the backdrop eat it.
      if (t == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::RightButton &&
          canvas_ && canvas_->hasImage()) {
        showContextMenu(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return true;
      }
      // Plain LEFT press on that same margin clears the selection — clicking empty
      // space inside the image already does (CanvasWidget::selectLineAt hit-tests to
      // nothing), but the backdrop around a zoomed-out image never reached the canvas,
      // so a selection got stuck there. Modifiers are excluded: Alt pans, Shift sweeps
      // a zoom rect and Ctrl+Shift multi-selects, and those gestures start on the
      // margin too. Browser counterpart: DrawingApp::deselectEmptyArea.
      if (t == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton &&
          static_cast<QMouseEvent*>(event)->modifiers() == Qt::NoModifier &&
          canvas_ && canvas_->hasImage() && !canvas_->compareReadOnly() &&
          !canvas_->selectedIndices().empty()) {
        canvas_->deselect();
        return true;
      }
      if (t == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
          const QPoint d = we->angleDelta();
          const int delta = d.y() != 0 ? d.y() : d.x();
          if (delta != 0) {
            const double step = (we->modifiers() & Qt::ShiftModifier) ? 0.3 : 0.1;
            setZoomAnchored(canvas_->scale() + (delta > 0 ? step : -step),
                            we->position().toPoint());
            return true;
          }
        }
        // Plain wheel over the margin → let the scroll area scroll.
      } else if (t == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
          const double factor = 1.0 + g->value();
          if (factor > 0.0 && factor != 1.0)
            setZoomAnchored(canvas_->scale() * factor, g->position().toPoint());
          return true;
        }
      }
    }
    // Hover-reveal for the name group: any Enter/Leave on the field or the ✎/🎨 buttons recomputes
    // hover (deferred so underMouse() settles — moving field→button stays "hovered", no flicker).
    if (obj == nameGroup_ || obj == projectName_ || obj == projectNameEdit_
        || obj == projectColorBtn_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Enter || t == QEvent::Leave)
        QTimer::singleShot(0, this, [this] { updateNameHover(); });
    }
    // …and a pointer that has landed ANYWHERE ELSE has left the group, whether or not the
    // group's own Leave arrived: crossing straight onto another row's icon left the ✎/🎨
    // lit while the pointer was three clusters away. Only
    // while the hover is actually held, so this costs nothing the rest of the time.
    if (nameHover_ && obj != nameGroup_ && obj != projectName_ && obj != projectNameEdit_
        && obj != projectColorBtn_
        && (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter
            || event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove))
      QTimer::singleShot(0, this, [this] { updateNameHover(); });
    if (obj == projectName_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::MouseButtonDblClick) {
        // Double-click a read-only name → enter edit mode (browser parity).
        if (!nameEditing_) {
          enterNameEdit();
          return true;
        }
      } else if (t == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
          // Escape always drops focus (clears the outline). If mid-edit, revert too.
          if (nameEditing_) cancelProjectName();
          else projectName_->clearFocus();
          return true;
        }
      } else if (t == QEvent::FocusOut) {
        // Clicking away leaves the edit: revert. Deferred so a click on ✓ commits first
        // (after which the field no longer has focus AND nameEditing_ is already false → no-op).
        if (nameEditing_) {
          QTimer::singleShot(0, this, [this] {
            if (nameEditing_ && projectName_ && !projectName_->hasFocus()) cancelProjectName();
          });
        }
      }
    }
    return QMainWindow::eventFilter(obj, event);
  }

}  // namespace stencil::gui
