#include "MainWindow.hpp"
#include "../../support/control/textFocus.hpp"
#include "../../support/uiTimings.hpp"
#include "ArrowPanner.hpp"
#include "CanvasWidget.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QAction>
#include <QToolButton>
#include <optional>

// MainWindow::eventFilter and the order of its chain (the stages are WindowEvents'), the window's own
// key handling, and the toolbar button a reveal hangs off.

namespace stencil::gui {

  namespace {
    // The key left of 1 (` / ~): Qt's key under a Latin layout, macOS's kVK_ANSI_Grave under any other.
    bool isTildeKey(const QKeyEvent* e) {
      if (e->key() == Qt::Key_QuoteLeft || e->key() == Qt::Key_AsciiTilde) return true;
#ifdef Q_OS_MACOS
      return e->nativeVirtualKey() == 0x32;
#else
      return false;
#endif
    }

    // A press on something that takes no focus, or Escape, blurs the field (browser parity).
    void blurFieldOn(QWidget* win, QObject* obj, QEvent* event) {
      QWidget* focus = QApplication::focusWidget();
      if (!focus || focus->window() != win || !support::isTextEntry(focus)) return;
      auto* w = qobject_cast<QWidget*>(obj);
      if (!w || w->window() != win) return;
      if (event->type() == QEvent::KeyPress) {
        const bool isOwn = w == focus || focus->isAncestorOf(w);
        if (isOwn && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
          QTimer::singleShot(0, focus, [focus] { focus->clearFocus(); });
        return;
      }
      for (QWidget* p = w; p; p = p->isWindow() ? nullptr : p->parentWidget())
        if (p->isEnabled() && (p->focusPolicy() & Qt::ClickFocus)) return;
      focus->clearFocus();
    }
  }  // namespace

  // A chain of handlers in THIS order: void ones observe, an optional-returning one that answers ends the chain.
  // tests/app/setup/MainWindow.composition.gui.cpp pins the verdicts.
  bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    parts.events.filterPointerChrome(obj, event);
    parts.events.filterDockChrome(obj, event);
    if (const auto r = parts.events.filterKeyClaims(obj, event)) return *r;
    if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress) && !pop.active)
      blurFieldOn(this, obj, event);
    if (const auto r = parts.events.filterPopoverGestures(obj, event)) return *r;
    if (const auto r = parts.events.filterPopoverButton(obj, event)) return *r;
    if (const auto r = parts.events.filterZoomAndLogo(obj, event)) return *r;
    if (const auto r = parts.events.filterCanvasViewport(obj, event)) return *r;
    if (const auto r = parts.events.filterProjectNameBar(obj, event)) return *r;
    return QMainWindow::eventFilter(obj, event);
  }

  // Arrow pan (drawingApp.js ~497): 7 px, 22 with Shift; Alt/Ctrl/Meta+arrows are reserved.
  void MainWindow::keyPressEvent(QKeyEvent* event) {
    const auto mods = event->modifiers();
    const int key = event->key();

    // Escape leaves fullscreen (browser parity).
    if (key == Qt::Key_Escape && fs.active) { parts.view.toggleFullscreen(); event->accept(); return; }

    // ~ pressed twice toggles drawing, beside Alt+A (browser drawDoublePress.js); a third press starts over.
    if (isTildeKey(event) && !(mods & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier))) {
      if (!event->isAutoRepeat()) {
        const quint64 at = event->timestamp();
        if (held.tildeAtMs && at - held.tildeAtMs <= quint64(support::uiTimings().doubleTapMs)) {
          held.tildeAtMs = 0;
          QAction* toggle = acts.startDraw->isEnabled() ? acts.startDraw : acts.stopDraw;
          if (toggle->isEnabled()) toggle->trigger();
        } else {
          held.tildeAtMs = at ? at : 1;
        }
      }
      event->accept();
      return;
    }

    // R held for the Alt+R+←/→ chord (browser #rHeld).
    if (key == Qt::Key_R) { held.r = true; QMainWindow::keyPressEvent(event); return; }

    // Alt+Shift+O peek needs key-up, so not a QAction; auto-repeat is ignored.
    if (key == Qt::Key_O && (mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier))) {
      if (!event->isAutoRepeat() && canvas->hasImage()) {
        canvas->setCompareHoldOriginal(true);
        refreshActions();   // read-only peek greys the editing actions too (parity with browser)
      }
      event->accept();
      return;
    }

    // Alt+R + ←/→ rotates the selection 3°/press (browser chord).
    if ((mods & Qt::AltModifier) && held.r && canvas->selectionCount() >= 1 &&
        !canvas->compareReadOnly() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
      constexpr double ROT_STEP = 3.14159265358979323846 / 60.0;  // 3° (matches wheel rotate)
      canvas->rotateSelectedLine((key == Qt::Key_Left ? -ROT_STEP : ROT_STEP));
      event->accept();
      return;
    }

    // Alt+Shift + arrow flips / rotates-90 the selection (browser parity); needs a selection and no read-only compare view.
    if ((mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
        canvas->selectionCount() >= 1 && !canvas->compareReadOnly() &&
        (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
         key == Qt::Key_Right)) {
      constexpr double QUARTER_TURN = 3.14159265358979323846 / 2.0;  // ±90° about the centre
      if (key == Qt::Key_Up) canvas->flipSelectedLine(true);
      else if (key == Qt::Key_Down) canvas->flipSelectedLine(false);
      else if (key == Qt::Key_Right) canvas->rotateSelectedLine(QUARTER_TURN);
      else canvas->rotateSelectedLine(-QUARTER_TURN);  // Key_Left
      event->accept();
      return;
    }

    if (mods & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier)) {
      QMainWindow::keyPressEvent(event);
      return;
    }

    int dirX = 0;
    int dirY = 0;
    if (key == Qt::Key_Left) dirX = -1;
    else if (key == Qt::Key_Right) dirX = 1;
    else if (key == Qt::Key_Up) dirY = -1;
    else if (key == Qt::Key_Down) dirY = 1;
    else if (key == Qt::Key_Shift) {
      arrowPan->setShift(true);   // read by the pan tick even without a fresh arrow press
      QMainWindow::keyPressEvent(event);
      return;
    }
    else { QMainWindow::keyPressEvent(event); return; }

    // With a selection, arrows NUDGE (1px, Shift = 10px), one axis per key (browser controlsBinder.js); a read-only compare view pans instead.
    if (canvas->selectionCount() >= 1 && !canvas->compareReadOnly()) {
      const int nStep = (mods & Qt::ShiftModifier) ? 10 : 1;
      canvas->nudgeSelected(dirX * nStep, dirY * nStep);
      event->accept();
      return;
    }
    // Record which arrow is down; the timer tick combines the held set so two arrows pan diagonally.
    arrowPan->press(dirX, dirY, mods & Qt::ShiftModifier);
    event->accept();
  }

  // Clear the held flags on release (or focus loss) so the chord and the pan never stick.
  void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_R) held.r = false;
    if (!event->isAutoRepeat()) arrowPan->release(event->key());
    if (!event->isAutoRepeat() && canvas->getCompareHoldOriginal() &&
        (event->key() == Qt::Key_O || event->key() == Qt::Key_Alt ||
         event->key() == Qt::Key_Shift || event->key() == Qt::Key_Meta ||
         event->key() == Qt::Key_Control)) {
      canvas->setCompareHoldOriginal(false);
      refreshActions();   // restore the editing actions on release (parity with browser)
    }
    QMainWindow::keyReleaseEvent(event);
  }

  // The visible toolbar button presenting `act`, to anchor the theme wipe.
  QWidget* MainWindow::buttonForAction(QAction* act) const {
    if (!act) return nullptr;
    for (QToolButton* b : findChildren<QToolButton*>())
      if (b->defaultAction() == act && b->isVisible()) return b;
    return nullptr;
  }

}  // namespace stencil::gui
