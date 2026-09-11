// Drag placement: the dock-zone poll, placement buttons, move/close/hide.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "../support/iconMotion.hpp"
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
    if (dockBtns_.size() < 4 || !floatBtn_ || !accentCache_.isValid()) return;
    static const char* kGlyphs[] = {"chevron-left", "chevron-up", "chevron-down",
                                    "chevron-right"};
    static const Qt::DockWidgetArea kAreas[] = {
        Qt::LeftDockWidgetArea, Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea,
        Qt::RightDockWidgetArea};
    auto* mw = qobject_cast<QMainWindow*>(parentWidget());
    const Qt::DockWidgetArea current =
        (!isFloating() && mw) ? mw->dockWidgetArea(this) : Qt::NoDockWidgetArea;
    const QString activeQss = QStringLiteral("background:%1;border:none;border-radius:5px;")
                                  .arg(chipCache_.name());
    const auto paint = [&](QToolButton* b, const char* glyph, bool active) {
      b->setIcon(themedIcon(glyph, active ? accentCache_ : textCache_, kHeaderIcon));
      b->setStyleSheet(active ? activeQss : QString());
      // The float button's `maximize` glyph has a second motion for the ALREADY-floating
      // state: its corners retract instead of extending (iconMotion.json variants.active).
      b->setProperty(kIconStateProperty, active ? "active" : "");
      // Left ENABLED: docking where you already are is a no-op anyway, and disabling it
      // handed the button to QToolButton:disabled — a bordered grey chip with a dimmed
      // glyph, which is what made the row look dark and unclear.
      b->setEnabled(true);
    };
    for (int i = 0; i < 4; ++i)
      paint(dockBtns_[i], kGlyphs[i], !isFloating() && current == kAreas[i]);
    paint(floatBtn_, "maximize", isFloating());
  }

  bool ChatDock::dragPollActive() const { return dragPoll_ && dragPoll_->isActive(); }

  bool ChatDock::dragActive() const { return manualDragging_ || dragPollActive(); }

  void ChatDock::setDragProbesForTest(std::function<QPoint()> cursorPos,
                                      std::function<bool()> leftButtonDown) {
    dragPosProbe_ = std::move(cursorPos);
    dragDownProbe_ = std::move(leftButtonDown);
  }

  void ChatDock::setNativeDockingSuppressed(bool on) {
    // With no allowed areas Qt can never show its drop placeholder or
    // hover-dock natively mid-drag — the zone overlay is the ONLY mechanism.
    // (addDockWidget on release happens AFTER the restore.)
    setAllowedAreas(on ? Qt::NoDockWidgetArea : Qt::AllDockWidgetAreas);
  }

  void ChatDock::startDragPoll() {
    if (!dragPoll_) {
      dragPoll_ = new QTimer(this);
      dragPoll_->setInterval(16);
      connect(dragPoll_, &QTimer::timeout, this, &ChatDock::pollDrag);
    }
    if (dragPoll_->isActive()) return;
    dragStartCursor_ = dragPosProbe_ ? dragPosProbe_() : QCursor::pos();
    dragMoved_ = false;
    dragActive_ = false;
    setNativeDockingSuppressed(true);
    dragPoll_->start();
  }

  void ChatDock::pollDrag() {
    const QPoint pos = dragPosProbe_ ? dragPosProbe_() : QCursor::pos();
    const bool down = dragDownProbe_
                          ? dragDownProbe_()
                          : QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    if (!dragMoved_ && (pos - dragStartCursor_).manhattanLength() >= 4)
      dragMoved_ = true;
    if (down) {
      // Past the platform drag threshold the dock is OURS: force (and keep)
      // it floating, so a tear-off from a docked side flows straight into the
      // zone flow and a native mid-drag dock can never stick (item 27).
      if (dragMoved_ && !isFloating() &&
          (pos - dragStartCursor_).manhattanLength() >=
              QApplication::startDragDistance())
        setFloating(true);
      // Zones only for a genuine FLOATING drag — a plain click never shows
      // them (and can therefore never dock on release).
      if (dragMoved_ && isFloating()) {
        if (!dragActive_) {
          dragActive_ = true;
          emit titleDragStarted();
        }
        emit titleDragMoved(pos);
      }
      return;
    }
    // Button released: restore native docking FIRST, then the LAST cursor
    // position decides the drop (zone docking or stay floating).
    dragPoll_->stop();
    setNativeDockingSuppressed(false);
    const bool wasActive = dragActive_;
    dragActive_ = false;
    if (wasActive) emit titleDragFinished(pos);
  }

  void ChatDock::cancelDragPoll() {
    if (dragPoll_) dragPoll_->stop();
    setNativeDockingSuppressed(false);  // never leave the dock undockable
    if (dragActive_) {
      dragActive_ = false;
      emit titleDragCanceled();
    }
  }

  void ChatDock::moveEvent(QMoveEvent* event) {
    QDockWidget::moveEvent(event);
    // A floating dock being dragged moves continuously — start the poll even
    // when the native drag consumed the title-bar press.
    const bool down = dragDownProbe_
                          ? dragDownProbe_()
                          : QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    // Not while WE drive the drag (the event path owns it end to end).
    if (isFloating() && down && !manualDrag_) startDragPoll();
  }

  void ChatDock::closeEvent(QCloseEvent* event) {
    // Hand the close to the owner (it animates, then hides us). With nobody
    // listening — or once the window is going away — fall back to Qt's own close,
    // so the dock can never become unclosable.
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
    // The float/dock transition re-parents the dock (a transient hide+show) —
    // cancel the drag poll only for a REAL hide (still hidden a tick later),
    // so a tear-off mid-drag keeps its poll; a closed dock never leaks it.
    QTimer::singleShot(0, this, [this] {
      if (!isVisible()) cancelDragPoll();
    });
  }
}  // namespace stencil::gui
