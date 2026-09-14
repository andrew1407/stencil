// Composer + title-bar event routing (ChatDock::eventFilter).
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "iconSet.hpp"
#include "theme.hpp"
#include "chatWidgets.hpp"

#include <QPlainTextEdit>
#include <QClipboard>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextCursor>
#include <QToolButton>

namespace stencil::gui {

  using namespace chatdock;
  bool ChatDock::eventFilter(QObject* obj, QEvent* event) {
    // Transcript viewport resized → re-cap the bubble widths (never consumed).
    if (scroll_ && obj == scroll_->viewport() && event->type() == QEvent::Resize) {
      applyBubbleWidths();
      positionJumpButtons();
    }
    // (The per-card "⋯" hover/placement lives in the shared ChatCardMore watcher
    // now — installChatCardMenu attaches one per card, on both surfaces.)
    // Jump pills: translucent at rest, full opacity under the cursor.
    if ((obj == jumpTop_ || obj == jumpBottom_) &&
        (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
      auto* pill = static_cast<QToolButton*>(obj);
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(pill->graphicsEffect());
      if (fx) fx->setOpacity(event->type() == QEvent::Enter ? 1.0 : GHOST_REST_OPACITY);
      // …and the glyph brightens to --text-main under the cursor, dropping back
      // to --text-muted (browser .chat-jump-btn / :hover).
      const QColor glyph = event->type() == QEvent::Enter
          ? (textCache_.isValid() ? textCache_ : palette().color(QPalette::Text))
          : (mutedCache_.isValid() ? mutedCache_ : palette().color(QPalette::PlaceholderText));
      pill->setIcon(themedIcon(pill == jumpTop_ ? "chevron-up" : "chevron-down", glyph, 14));
    }
    // The composer IS the drop target (the dock itself declines drops)
    if (obj == inputArea_) {
      // The cue and the attach belong to the INPUT BOX only (browser parity): a
      // drag over the composer's buttons or chips neither lights the cue nor
      // attaches — that drop falls through to the dock, which swallows it.
      const auto overInput = [this](const QPointF& p) {
        if (!input_ || !input_->isVisible()) return true;
        return QRect(input_->mapTo(inputArea_, QPoint(0, 0)), input_->size())
            .contains(p.toPoint());
      };
      switch (event->type()) {
        case QEvent::DragEnter: {
          auto* de = static_cast<QDragEnterEvent*>(event);
          if (!canAttachMime(de->mimeData())) return false;   // let it fall through
          de->acceptProposedAction();
          showDropCue(overInput(de->position()));
          return true;
        }
        case QEvent::DragMove: {
          auto* dm = static_cast<QDragMoveEvent*>(event);
          if (!canAttachMime(dm->mimeData())) return false;
          dm->acceptProposedAction();
          showDropCue(overInput(dm->position()));
          return true;
        }
        case QEvent::DragLeave:
          showDropCue(false);
          return true;
        case QEvent::Drop: {
          auto* dr = static_cast<QDropEvent*>(event);
          showDropCue(false);
          if (!overInput(dr->position())) return false;   // dock swallows the miss
          if (!attachFromMimeData(dr->mimeData())) return false;
          dr->acceptProposedAction();
          return true;
        }
        case QEvent::Resize:
          // The cue tracks the input's box as the composer resizes (never consumed).
          if (dropCue_ && dropCue_->isVisible()) showDropCue(true);
          break;
        default:
          break;
      }
    }
    // Title-bar press starts the drag POLL (never consumed — Qt's own dock
    // drag runs on the same press). Tracking is poll-based because the native
    // floating-window drag swallows the subsequent move/release events.
    if (obj == titleBar_) {
      /* Browser parity (ui/chatDock.js): the compact popover's bar DRAGS like any other, and
       * the drag adopts the layout — titleDragStarted clears the compact flag, so what moves
       * is a float the user chose, not a popover still pinned to its icon. Only the
       * double-click float/dock toggle stays swallowed: the browser's header has none. */
      if (compactPopover_ && event->type() == QEvent::MouseButtonDblClick) return true;
      // Test seam installed (offscreen, no real cursor) → the poll path.
      if (dragPosProbe_) {
        if (event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
          startDragPoll();
        return QDockWidget::eventFilter(obj, event);
      }
      // Real input: drive the drag ourselves and CONSUME the events, so Qt
      // never starts its own move. Qt's floating-dock drag becomes a
      // window-server move on macOS, which swallows the release — the drop
      // then never resolves and nothing docks.
      switch (event->type()) {
        case QEvent::MouseButtonPress: {
          auto* me = static_cast<QMouseEvent*>(event);
          if (me->button() != Qt::LeftButton) break;
          manualDrag_ = true;
          manualDragging_ = false;
          titleBar_->setCursor(Qt::OpenHandCursor);   // drag ended — back to "grab me"
          dragStartCursor_ = me->globalPosition().toPoint();
          // Explicit grab: once the cursor leaves the bar the moves would
          // otherwise be delivered to whatever is underneath, and the drag
          // (and its zones) would never start.
          titleBar_->grabMouse();
          return true;
        }
        case QEvent::MouseMove: {
          if (!manualDrag_) break;
          const QPoint g = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
          if (!manualDragging_) {
            if ((g - dragStartCursor_).manhattanLength() < QApplication::startDragDistance())
              return true;
            manualDragging_ = true;
            titleBar_->setCursor(Qt::ClosedHandCursor);
            setNativeDockingSuppressed(true);
            if (!isFloating()) {
              // Tear off under the cursor, at the compact float default —
              // the browser's undock-at-the-pointer behaviour.
              setFloating(true);
              resize(FLOATING_SIZE);
              manualGrabOffset_ = QPoint(qMin(FLOATING_SIZE.width() / 2, 140), 12);
            } else {
              manualGrabOffset_ = g - frameGeometry().topLeft();
            }
            dragActive_ = true;
            emit titleDragStarted();
          }
          move(g - manualGrabOffset_);
          emit titleDragMoved(g);
          return true;
        }
        case QEvent::MouseButtonRelease: {
          if (!manualDrag_) break;
          const QPoint g = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
          const bool dragged = manualDragging_;
          manualDrag_ = manualDragging_ = false;
          titleBar_->setCursor(Qt::OpenHandCursor);   // drag ended — back to "grab me"
          titleBar_->releaseMouse();
          setNativeDockingSuppressed(false);   // dock AFTER the restore
          if (dragged) {
            dragActive_ = false;
            emit titleDragFinished(g);
          }
          return true;
        }
        default:
          break;
      }
      return QDockWidget::eventFilter(obj, event);
    }
    if (obj == input_ && event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      // Paste: an image on the clipboard (or image/video file URLs) becomes an
      // attachment; plain text falls through to the normal paste.
      if (ke->matches(QKeySequence::Paste)) {
        if (attachFromMimeData(QGuiApplication::clipboard()->mimeData())) return true;
        return QDockWidget::eventFilter(obj, event);
      }
      // Word delete, spelled out rather than left to the platform's standard-key table: ⌥⌫ is
      // what people press here, and Qt's mapping for it varies by platform (on this one it
      // arrived as a plain Backspace and ate a single character). ⌥⌦ deletes forward.
      if ((ke->key() == Qt::Key_Backspace || ke->key() == Qt::Key_Delete) &&
          (ke->modifiers() & (Qt::AltModifier | Qt::ControlModifier))) {
        const auto toward = ke->key() == Qt::Key_Backspace ? QTextCursor::PreviousWord
                                                           : QTextCursor::NextWord;
        QTextCursor c = input_->textCursor();
        if (!c.hasSelection()) c.movePosition(toward, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        input_->setTextCursor(c);
        return true;
      }
      // Enter sends; Shift+Enter inserts a newline (browser textarea convention).
      if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
          !(ke->modifiers() & Qt::ShiftModifier)) {
        submit();
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }
}  // namespace stencil::gui
