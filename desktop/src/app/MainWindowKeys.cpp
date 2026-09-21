#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "StayOpenMenu.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "OverlayScrollArea.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "SelectionPanel.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
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
    if (key == Qt::Key_Escape && fs.active) { toggleFullscreen(); event->accept(); return; }

    // R held for the Alt+R+←/→ chord (browser #rHeld).
    if (key == Qt::Key_R) { rKeyHeld = true; QMainWindow::keyPressEvent(event); return; }

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
    if ((mods & Qt::AltModifier) && rKeyHeld && canvas->selectionCount() >= 1 &&
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
      panShiftHeld = true;   // read by the pan tick even without a fresh arrow press
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
    if (dirX < 0) panLeftHeld = true;
    else if (dirX > 0) panRightHeld = true;
    if (dirY < 0) panUpHeld = true;
    else if (dirY > 0) panDownHeld = true;
    panShiftHeld = mods & Qt::ShiftModifier;
    if (arrowPanTimer && !arrowPanTimer->isActive()) arrowPanTimer->start();
    event->accept();
  }

  // Clear the held flags on release (or focus loss) so the chord and the pan never stick.
  void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_R) rKeyHeld = false;
    if (!event->isAutoRepeat()) {
      switch (event->key()) {
        case Qt::Key_Left: panLeftHeld = false; break;
        case Qt::Key_Right: panRightHeld = false; break;
        case Qt::Key_Up: panUpHeld = false; break;
        case Qt::Key_Down: panDownHeld = false; break;
        case Qt::Key_Shift: panShiftHeld = false; break;
        default: break;
      }
    }
    if (!event->isAutoRepeat() && canvas->getCompareHoldOriginal() &&
        (event->key() == Qt::Key_O || event->key() == Qt::Key_Alt ||
         event->key() == Qt::Key_Shift || event->key() == Qt::Key_Meta ||
         event->key() == Qt::Key_Control)) {
      canvas->setCompareHoldOriginal(false);
      refreshActions();   // restore the editing actions on release (parity with browser)
    }
    QMainWindow::keyReleaseEvent(event);
  }

  // Visible unless the IMAGE cluster is in its empty state: with no image only the labelled Open button shows (browser #image-actions).
  bool MainWindow::sectionButtonVisible(QAction* act, QToolButton* btn) const {
    if (act && !act->isVisible()) return false;
    if (imageSection && btn && btn != openImageBtn && imageSection->isAncestorOf(btn))
      return canvas && canvas->hasImage();
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
      pop.dialogAnchor = buttonForAction(a);   // resolved at trigger time; buttons come later
      pop.dialogAnchorRect = (pop.menuRowAction == a) ? pop.menuRowRect : QRect();
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
