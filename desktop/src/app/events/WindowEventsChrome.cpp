// eventFilter chain: the observers (pointer chrome + dock chrome), which never consume, and the key
// claims. Order and verdicts: MainWindowEvents.cpp.
#include "MainWindow.hpp"
#include "WindowEvents.hpp"
#include "ArrowPanner.hpp"
#include "DropZonesOverlay.hpp"
#include "SelectionPanel.hpp"
#include "ChatDock.hpp"

#include <QScrollBar>
#include <QWindow>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>

namespace stencil::gui {

  void WindowEvents::filterPointerChrome(QObject* obj, QEvent* event) {
    // Hovering a scrollbar directly must never let it fade out from under the cursor.
    if (w.scroll && (obj == w.parts.view.canvasScrollBar(Qt::Horizontal) || obj == w.parts.view.canvasScrollBar(Qt::Vertical))) {
      if (event->type() == QEvent::Enter) { w.parts.view.scrollbarHovered = true; w.parts.view.revealCanvasScrollbars(); }
      else if (event->type() == QEvent::Leave) { w.parts.view.scrollbarHovered = false; w.parts.view.scheduleScrollbarHide(); }
    }
    if (obj == w.chatDock && event->type() == QEvent::Resize) w.parts.dockChrome.syncToastInset();
    // A child that accepts drops (the chat dock) becomes the drag's target, and the window is
    // then sent no move at all — so the zones follow the drag wherever Qt delivers it.
    if (w.overlays.dropZones && !w.overlays.dropZones->isHidden() && obj != &w &&
        (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
      if (auto* over = qobject_cast<QWidget*>(obj))
        w.overlays.dropZones->followDrag(
            over->mapToGlobal(static_cast<QDragMoveEvent*>(event)->position().toPoint()));
    }
    // Disabled controls show `not-allowed` (browser rule): Qt never sends a disabled widget the move, so an app-wide filter + override cursor is the one way.
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter ||
        event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverMove) {
      auto* widget = qobject_cast<QWidget*>(obj);
      QWidget* row = nullptr;         // the toolbar row this event is about, if any
      bool overDead = false;
      if (widget && widget->parentWidget() && widget->parentWidget()->property("toolRow").toBool()) {
        row = widget->parentWidget();                                     // the discarded-move path
        overDead = !widget->isEnabled();
      } else if (widget && widget->property("toolRow").toBool() && event->type() == QEvent::MouseMove) {
        // The row's gaps too, hit-tested by hand — childAt() skips the widget the mouse cannot reach.
        row = widget;
        const QPoint p = static_cast<QMouseEvent*>(event)->position().toPoint();
        for (QObject* c : widget->children()) {
          auto* cw = qobject_cast<QWidget*>(c);
          if (!cw || !cw->isVisible() || !cw->geometry().contains(p)) continue;
          overDead = !cw->isEnabled();
          break;
        }
      }
      if (row) {
        w.blocked.set(overDead);
        w.blocked.row = overDead ? row : nullptr;
      } else if (event->type() == QEvent::MouseMove && w.blocked.pushed && widget &&
                 !(w.blocked.row && widget->isAncestorOf(w.blocked.row))) {
        // One physical move is delivered again to each ANCESTOR; treating those copies as "somewhere else" flipped the cursor off and on.
        w.blocked.set(false);
        w.blocked.row = nullptr;
      }
    }
    if (event->type() == QEvent::Leave || event->type() == QEvent::WindowDeactivate ||
        (obj == &w && (event->type() == QEvent::Hide || event->type() == QEvent::Close)))
      w.blocked.set(false);
    // Fullscreen's edge reveal: the window's QWindow hears every move, tracked widget or not.
    const QEvent::Type t = event->type();
    if (w.fs.active && !w.fs.tickQueued && (t == QEvent::MouseMove || t == QEvent::HoverMove || t == QEvent::Enter)) {
      auto* widget = qobject_cast<QWidget*>(obj);
      if (obj == w.windowHandle() || (widget && widget->window() == &w)) {
        w.fs.tickQueued = true;
        QTimer::singleShot(0, &w, [this] {
          w.fs.tickQueued = false;
          w.parts.view.fsHoverTick();
        });
      }
    }
  }

  void WindowEvents::filterDockChrome(QObject* obj, QEvent* event) {
    // The grip must FOLLOW the panel through every geometry change, not only viewport resizes.
    if (obj == w.selPanel && w.parts.dockChrome.panelGrip) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Show || t == QEvent::Hide)
        w.parts.dockChrome.positionPanelGrip();
    }
    if (obj == w.chatDock && w.parts.dockChrome.chatEdge) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Show || t == QEvent::Hide)
        w.parts.dockChrome.positionChatEdge();
    }
    // A lost-focus window never delivers the held arrows' key-up (browser controlsBinder.js blur listener).
    if (obj == &w && event->type() == QEvent::WindowDeactivate) {
      if (w.arrowPan) w.arrowPan->stop();
    }
  }

  namespace {
    bool isTextEntry(QObject* obj) {
      return qobject_cast<QLineEdit*>(obj) || qobject_cast<QPlainTextEdit*>(obj) ||
             qobject_cast<QTextEdit*>(obj);
    }
  }  // namespace

  std::optional<bool> WindowEvents::filterKeyClaims(QObject* obj, QEvent* event) {
    // While a TEXT BOX has focus the editing chords belong to it (⌥⌫ deleted the selected LINE mid-typing); claiming ShortcutOverride hands the key back.
    if (event->type() == QEvent::ShortcutOverride && isTextEntry(obj)) {
      auto* ke = static_cast<QKeyEvent*>(event);
      static const QKeySequence::StandardKey EDITING[] = {
          QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord,
          QKeySequence::DeleteCompleteLine, QKeySequence::MoveToPreviousWord,
          QKeySequence::MoveToNextWord,     QKeySequence::SelectPreviousWord,
          QKeySequence::SelectNextWord,     QKeySequence::MoveToStartOfLine,
          QKeySequence::MoveToEndOfLine,    QKeySequence::SelectStartOfLine,
          QKeySequence::SelectEndOfLine,    QKeySequence::Undo,
          QKeySequence::Redo,               QKeySequence::SelectAll};
      for (const auto key : EDITING) {
        if (ke->matches(key)) {
          event->accept();
          return true;
        }
      }
    }
    // Escape, app-wide (KeyPress AND ShortcutOverride): closes a popover first, then leaves fullscreen (fs.active — isFullScreen() is unreliable on macOS).
    if (w.pop.active &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      auto* widget = qobject_cast<QWidget*>(obj);
      if (widget && (widget->window() == &w || widget == w.pop.active.data() ||
                w.pop.active->isAncestorOf(widget))) {
        w.pop.peekAction.clear();   // Escape is deliberate: it ends a peek for good
        w.dismissPopover();
        return true;
      }
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && w.chatCompactShowing()) {
      if (event->type() == QEvent::KeyPress && w.acts.chat) w.acts.chat->setChecked(false);
      return true;   // the ShortcutOverride claim keeps focused widgets from eating it
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && w.fs.active) {
      w.parts.view.toggleFullscreen();
      return true;
    }
    return {};   // nothing here answered — the chain goes on
  }

}  // namespace stencil::gui
