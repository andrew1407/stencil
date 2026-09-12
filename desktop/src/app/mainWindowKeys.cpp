#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "stayOpenMenu.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "chatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "overlayScrollArea.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/modalChrome.hpp"

#include <QAction>
#include <QKeyEvent>
#include <QToolButton>

// Key handling and the reveal anchors that hang off toolbar buttons.

namespace stencil::gui {

  // Arrow pan (drawingApp.js ~497): 7 px, 22 with Shift; Alt/Ctrl/Meta+arrows are reserved.
  void MainWindow::keyPressEvent(QKeyEvent* event) {
    const auto mods = event->modifiers();
    const int key = event->key();

    // Escape leaves fullscreen (browser parity).
    if (key == Qt::Key_Escape && fs_.active) { toggleFullscreen(); event->accept(); return; }

    // R held for the Alt+R+←/→ chord (browser #rHeld).
    if (key == Qt::Key_R) { rKeyHeld_ = true; QMainWindow::keyPressEvent(event); return; }

    // Alt+Shift+O peek needs key-up, so not a QAction; auto-repeat is ignored.
    if (key == Qt::Key_O && (mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier))) {
      if (!event->isAutoRepeat() && canvas_->hasImage()) {
        canvas_->setCompareHoldOriginal(true);
        refreshActions();   // read-only peek greys the editing actions too (parity with browser)
      }
      event->accept();
      return;
    }

    // Alt+R + ←/→ rotates the selection 3°/press (browser chord).
    if ((mods & Qt::AltModifier) && rKeyHeld_ && canvas_->selectionCount() >= 1 &&
        !canvas_->compareReadOnly() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
      constexpr double ROT_STEP = 3.14159265358979323846 / 60.0;  // 3° (matches wheel rotate)
      canvas_->rotateSelectedLine((key == Qt::Key_Left ? -ROT_STEP : ROT_STEP));
      event->accept();
      return;
    }

    // Alt+Shift + arrow flips / rotates-90 the selection (browser parity); needs a selection and no read-only compare view.
    if ((mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
        canvas_->selectionCount() >= 1 && !canvas_->compareReadOnly() &&
        (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
         key == Qt::Key_Right)) {
      constexpr double QUARTER_TURN = 3.14159265358979323846 / 2.0;  // ±90° about the centre
      if (key == Qt::Key_Up) canvas_->flipSelectedLine(true);
      else if (key == Qt::Key_Down) canvas_->flipSelectedLine(false);
      else if (key == Qt::Key_Right) canvas_->rotateSelectedLine(QUARTER_TURN);
      else canvas_->rotateSelectedLine(-QUARTER_TURN);  // Key_Left
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
      panShiftHeld_ = true;   // read by the pan tick even without a fresh arrow press
      QMainWindow::keyPressEvent(event);
      return;
    }
    else { QMainWindow::keyPressEvent(event); return; }

    // With a selection, arrows NUDGE (1px, Shift = 10px), one axis per key (browser controlsBinder.js); a read-only compare view pans instead.
    if (canvas_->selectionCount() >= 1 && !canvas_->compareReadOnly()) {
      const int nStep = (mods & Qt::ShiftModifier) ? 10 : 1;
      canvas_->nudgeSelected(dirX * nStep, dirY * nStep);
      event->accept();
      return;
    }
    // Record which arrow is down; the timer tick combines the held set so two arrows pan diagonally.
    if (dirX < 0) panLeftHeld_ = true;
    else if (dirX > 0) panRightHeld_ = true;
    if (dirY < 0) panUpHeld_ = true;
    else if (dirY > 0) panDownHeld_ = true;
    panShiftHeld_ = mods & Qt::ShiftModifier;
    if (arrowPanTimer_ && !arrowPanTimer_->isActive()) arrowPanTimer_->start();
    event->accept();
  }

  // Clear the held flags on release (or focus loss) so the chord and the pan never stick.
  void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_R) rKeyHeld_ = false;
    if (!event->isAutoRepeat()) {
      switch (event->key()) {
        case Qt::Key_Left: panLeftHeld_ = false; break;
        case Qt::Key_Right: panRightHeld_ = false; break;
        case Qt::Key_Up: panUpHeld_ = false; break;
        case Qt::Key_Down: panDownHeld_ = false; break;
        case Qt::Key_Shift: panShiftHeld_ = false; break;
        default: break;
      }
    }
    if (!event->isAutoRepeat() && canvas_->compareHoldOriginal() &&
        (event->key() == Qt::Key_O || event->key() == Qt::Key_Alt ||
         event->key() == Qt::Key_Shift || event->key() == Qt::Key_Meta ||
         event->key() == Qt::Key_Control)) {
      canvas_->setCompareHoldOriginal(false);
      refreshActions();   // restore the editing actions on release (parity with browser)
    }
    QMainWindow::keyReleaseEvent(event);
  }

  // Visible unless the IMAGE cluster is in its empty state: with no image only the labelled Open button shows (browser #image-actions).
  bool MainWindow::sectionButtonVisible(QAction* act, QToolButton* btn) const {
    if (act && !act->isVisible()) return false;
    if (imageSection_ && btn && btn != openImageBtn_ && imageSection_->isAncestorOf(btn))
      return canvas_ && canvas_->hasImage();
    return true;
  }

  // Every action records its own origin when it fires; clearing it for a button-less action stops a dialog flying out of the last icon used.
  void MainWindow::bindRevealAnchors() {
    for (QAction* a : findChildren<QAction*>()) bindRevealAnchor(a);
  }

  // Bound at creation so it is the first triggered() slot; bound after the handlers it ran only once exec() had returned.
  void MainWindow::bindRevealAnchor(QAction* a) {
    if (!a || a->property("revealBound").toBool()) return;
    a->setProperty("revealBound", true);
    connect(a, &QAction::triggered, this, [this, a] {
      pop_.dialogAnchor = buttonForAction(a);   // resolved at trigger time; buttons come later
      pop_.dialogAnchorRect = (pop_.menuRowAction == a) ? pop_.menuRowRect : QRect();
    });
  }

  // The visible toolbar button presenting `act`, to anchor the theme wipe.
  QWidget* MainWindow::buttonForAction(QAction* act) const {
    if (!act) return nullptr;
    for (QToolButton* b : findChildren<QToolButton*>())
      if (b->defaultAction() == act && b->isVisible()) return b;
    return nullptr;
  }

}  // namespace stencil::gui
