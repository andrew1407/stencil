// Drag placement: the dock-zone poll, placement buttons, move/close/hide.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../../../support/icon/iconMotion.hpp"
#include "iconSet.hpp"

#include <QApplication>
#include <QCursor>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QTimer>
#include <QToolButton>
#include <QMoveEvent>
#include <QHideEvent>
#include <functional>

namespace stencil::gui {

  using namespace chatdock;
  // drag-poll (drag dock zones; see the header comment)

  // The button for the CURRENT placement is accent-filled and inert (browser
  // .chat-dock-btn-active) — you can't dock where you already are.
  void ChatDock::updatePlacementState() {
    if (chrome.dockBtns.size() < 4 || !chrome.floatBtn || !accentCache.isValid()) return;
    static const char* GLYPHS[] = {"chevron-left", "chevron-up", "chevron-down",
                                    "chevron-right"};
    static const Qt::DockWidgetArea AREAS[] = {
        Qt::LeftDockWidgetArea, Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea,
        Qt::RightDockWidgetArea};
    auto* mw = qobject_cast<QMainWindow*>(parentWidget());
    const Qt::DockWidgetArea current =
        (!isFloating() && mw) ? mw->dockWidgetArea(this) : Qt::NoDockWidgetArea;
    const QString activeQss = QStringLiteral("background:%1;border:none;border-radius:5px;")
                                  .arg(chipCache.name());
    const auto paint = [&](QToolButton* b, const char* glyph, bool active) {
      b->setIcon(themedIcon(glyph, active ? accentCache : textCache, HEADER_ICON));
      b->setStyleSheet(active ? activeQss : QString());
      // The float button's `maximize` glyph has a second motion for the ALREADY-floating
      // state: its corners retract instead of extending (iconMotion.json variants.active).
      b->setProperty(ICON_STATE_PROPERTY, active ? "active" : "");
      // Left ENABLED: docking where you already are is a no-op anyway, and disabling it handed the
      // button to QToolButton:disabled - a bordered grey chip with a dimmed glyph.
      b->setEnabled(true);
    };
    for (int i = 0; i < 4; ++i)
      paint(chrome.dockBtns[i], GLYPHS[i], !isFloating() && current == AREAS[i]);
    paint(chrome.floatBtn, "maximize", isFloating());
  }

  // Entering compact cancels a press that never became a move, so the grab cannot strand.
  // The cursor stays the open hand either way: a compact popover is draggable too.
  void ChatDock::setCompactPopover(bool on) {
    if (compactPopover == on) return;
    compactPopover = on;
    if (on && manualDrag) {
      manualDrag = manualDragging = false;
      if (chrome.titleBar) chrome.titleBar->releaseMouse();
    }
  }

  bool ChatDock::dragPollActive() const { return dragPoll && dragPoll->isActive(); }

  bool ChatDock::getDragActive() const { return manualDragging || dragPollActive(); }

  void ChatDock::setDragProbesForTest(std::function<QPoint()> cursorPos,
                                      std::function<bool()> leftButtonDown) {
    dragPosProbe = std::move(cursorPos);
    dragDownProbe = std::move(leftButtonDown);
  }

  void ChatDock::setNativeDockingSuppressed(bool on) {
    // With no allowed areas Qt can never show its drop placeholder or hover-dock natively mid-drag -
    // the zone overlay is the ONLY mechanism. (addDockWidget on release happens AFTER the restore.)
    setAllowedAreas(on ? Qt::NoDockWidgetArea : Qt::AllDockWidgetAreas);
  }

  void ChatDock::startDragPoll() {
    if (!dragPoll) {
      dragPoll = new QTimer(this);
      dragPoll->setInterval(16);
      connect(dragPoll, &QTimer::timeout, this, &ChatDock::pollDrag);
    }
    if (dragPoll->isActive()) return;
    dragStartCursor = dragPosProbe ? dragPosProbe() : QCursor::pos();
    dragMoved = false;
    dragActive = false;
    setNativeDockingSuppressed(true);
    dragPoll->start();
  }

  void ChatDock::pollDrag() {
    const QPoint pos = dragPosProbe ? dragPosProbe() : QCursor::pos();
    const bool down = dragDownProbe
                          ? dragDownProbe()
                          : QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    if (!dragMoved && (pos - dragStartCursor).manhattanLength() >= 4)
      dragMoved = true;
    if (down) {
      // Past the platform drag threshold the dock is OURS: force (and keep) it floating, so a tear-off
      // flows straight into the zone flow and a native mid-drag dock can never stick.
      if (dragMoved && !isFloating() &&
          (pos - dragStartCursor).manhattanLength() >=
              QApplication::startDragDistance())
        setFloating(true);
      // Zones only for a genuine FLOATING drag — a plain click never shows
      // them (and can therefore never dock on release).
      if (dragMoved && isFloating()) {
        if (!dragActive) {
          dragActive = true;
          emit titleDragStarted();
        }
        emit titleDragMoved(pos);
      }
      return;
    }
    // Button released: restore native docking FIRST, then the LAST cursor
    // position decides the drop (zone docking or stay floating).
    dragPoll->stop();
    setNativeDockingSuppressed(false);
    const bool wasActive = dragActive;
    dragActive = false;
    if (wasActive) emit titleDragFinished(pos);
  }

  void ChatDock::cancelDragPoll() {
    if (dragPoll) dragPoll->stop();
    setNativeDockingSuppressed(false);  // never leave the dock undockable
    if (dragActive) {
      dragActive = false;
      emit titleDragCanceled();
    }
  }

  void ChatDock::moveEvent(QMoveEvent* event) {
    QDockWidget::moveEvent(event);
    // A floating dock being dragged moves continuously — start the poll even
    // when the native drag consumed the title-bar press.
    const bool down = dragDownProbe
                          ? dragDownProbe()
                          : QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    // Not while WE drive the drag (the event path owns it end to end).
    if (isFloating() && down && !manualDrag) startDragPoll();
  }

  void ChatDock::closeEvent(QCloseEvent* event) {
    // Hand the close to the owner (it animates, then hides us). With nobody listening - or once the
    // window is going away - fall back to Qt's own close, so the dock can never become unclosable.
    if (receivers(SIGNAL(closeRequested())) > 0 && isVisible() && window() &&
        window()->isVisible()) {
      event->ignore();
      emit closeRequested();
      return;
    }
    QDockWidget::closeEvent(event);
  }

  void ChatDock::hideEvent(QHideEvent* event) {
    QDockWidget::hideEvent(event);
    // The float/dock transition re-parents the dock (a transient hide+show), so cancel the drag poll
    // only for a REAL hide (still hidden a tick later); a closed dock never leaks it.
    QTimer::singleShot(0, this, [this] {
      if (!isVisible()) cancelDragPoll();
    });
  }
}  // namespace stencil::gui
