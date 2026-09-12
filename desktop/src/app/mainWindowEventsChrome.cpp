// eventFilter chain, observers only (pointer chrome + dock chrome). Neither consumes — order in mainWindowEvents.cpp.
#include "mainWindow.hpp"
#include "chatDock.hpp"
#include "selectionPanel.hpp"
#include "../support/dockGrip.hpp"
#include <QEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>

namespace stencil::gui {

  void MainWindow::filterPointerChrome(QObject* obj, QEvent* event) {
    // Hovering a scrollbar directly must never let it fade out from under the cursor.
    if (scroll_ && (obj == canvasScrollBar(Qt::Horizontal) || obj == canvasScrollBar(Qt::Vertical))) {
      if (event->type() == QEvent::Enter) { scrollbarHovered_ = true; revealCanvasScrollbars(); }
      else if (event->type() == QEvent::Leave) { scrollbarHovered_ = false; scheduleScrollbarHide(); }
    }
    if (obj == chatDock_ && event->type() == QEvent::Resize) syncToastInset();
    // Disabled controls show `not-allowed` (browser rule): Qt never sends a disabled widget the move, so an app-wide filter + override cursor is the one way.
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter ||
        event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverMove) {
      auto* w = qobject_cast<QWidget*>(obj);
      QWidget* row = nullptr;         // the toolbar row this event is about, if any
      bool overDead = false;
      if (w && w->parentWidget() && w->parentWidget()->property("toolRow").toBool()) {
        row = w->parentWidget();                                     // the discarded-move path
        overDead = !w->isEnabled();
      } else if (w && w->property("toolRow").toBool() && event->type() == QEvent::MouseMove) {
        // The row's gaps too, hit-tested by hand — childAt() skips the widget the mouse cannot reach.
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
        // One physical move is delivered again to each ANCESTOR; treating those copies as "somewhere else" flipped the cursor off and on.
        setBlockedCursor(false);
        blockedRow_ = nullptr;
      }
    }
    if (event->type() == QEvent::Leave || event->type() == QEvent::WindowDeactivate ||
        (obj == this && (event->type() == QEvent::Hide || event->type() == QEvent::Close)))
      setBlockedCursor(false);
  }

  void MainWindow::filterDockChrome(QObject* obj, QEvent* event) {
    // The grip must FOLLOW the panel through every geometry change, not only viewport resizes.
    if (obj == selPanel_ && panelGrip_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Show || t == QEvent::Hide)
        positionPanelGrip();
    }
    if (obj == chatDock_ && chatEdge_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Show || t == QEvent::Hide)
        positionChatEdge();
    }
    // The separator belongs to the QMainWindow itself, so ITS hover and drag arrive here; hot through a drag (browser .panel-resizer `.dragging`).
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
    // The chat dock's resize edge (browser .chat-resizer), hit-tested against the separator's own rect, not the painted band.
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
    // A lost-focus window never delivers the held arrows' key-up (browser controlsBinder.js blur listener).
    if (obj == this && event->type() == QEvent::WindowDeactivate) {
      panLeftHeld_ = panRightHeld_ = panUpHeld_ = panDownHeld_ = panShiftHeld_ = false;
      if (arrowPanTimer_) arrowPanTimer_->stop();
    }
  }

}  // namespace stencil::gui
