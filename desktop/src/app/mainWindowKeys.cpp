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

  // Arrow keys pan the viewport (drawingApp.js arrow-pan ~497): 7 px, or 22
  // with Shift. Alt/Ctrl/Meta+arrows are reserved (don't pan).
  void MainWindow::keyPressEvent(QKeyEvent* event) {
    const auto mods = event->modifiers();
    const int key = event->key();

    // Escape leaves fullscreen (browser parity) — restores the toolbars + panel.
    if (key == Qt::Key_Escape && fs_.active) { toggleFullscreen(); event->accept(); return; }

    // Track R held for the Alt+R+←/→ line-rotate chord (mirror of the browser #rHeld).
    if (key == Qt::Key_R) { rKeyHeld_ = true; QMainWindow::keyPressEvent(event); return; }

    // Alt+Shift+O — momentary "peek at the original" (mirror of the browser hold). Handled
    // here rather than as a QAction because it needs key-up; auto-repeat is ignored.
    if (key == Qt::Key_O && (mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier))) {
      if (!event->isAutoRepeat() && canvas_->hasImage()) {
        canvas_->setCompareHoldOriginal(true);
        refreshActions();   // read-only peek greys the editing actions too (parity with browser)
      }
      event->accept();
      return;
    }

    // Alt+R + ←/→ → rotate the selected line(s) (← CCW, → CW), 3°/press. Mirrors the browser
    // chord and the Ctrl+Shift+wheel rotate. Only fires when something is selected.
    if ((mods & Qt::AltModifier) && rKeyHeld_ && canvas_->selectionCount() >= 1 &&
        !canvas_->compareReadOnly() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
      constexpr double kRotStep = 3.14159265358979323846 / 60.0;  // 3° (matches wheel rotate)
      canvas_->rotateSelectedLine((key == Qt::Key_Left ? -kRotStep : kRotStep));
      event->accept();
      return;
    }

    // Alt+Shift + arrow → flip / rotate-90 the selected line(s) about the selection's
    // bounding-box centre (browser parity: ↑ flip horizontal, ↓ flip vertical,
    // → rotate +90°, ← rotate −90°). Rotate-90 reuses the arbitrary-angle rotate path.
    // Only fires with a selection and outside a read-only compare view (mirrors Alt+R).
    if ((mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
        canvas_->selectionCount() >= 1 && !canvas_->compareReadOnly() &&
        (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
         key == Qt::Key_Right)) {
      constexpr double kQuarterTurn = 3.14159265358979323846 / 2.0;  // ±90° about the centre
      if (key == Qt::Key_Up) canvas_->flipSelectedLine(true);
      else if (key == Qt::Key_Down) canvas_->flipSelectedLine(false);
      else if (key == Qt::Key_Right) canvas_->rotateSelectedLine(kQuarterTurn);
      else canvas_->rotateSelectedLine(-kQuarterTurn);  // Key_Left
      event->accept();
      return;
    }

    // Other Alt/Ctrl/Meta+arrow combos stay reserved (e.g. zoom).
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

    // With a line selected, arrows NUDGE the selection (1px, Shift = 10px, image space) —
    // one axis per key, matching the browser's own nudge (controlsBinder.js: also a plain
    // if/else-if on e.key, not combined). A read-only compare view disables the nudge —
    // arrows always pan there instead.
    if (canvas_->selectionCount() >= 1 && !canvas_->compareReadOnly()) {
      const int nStep = (mods & Qt::ShiftModifier) ? 10 : 1;
      canvas_->nudgeSelected(dirX * nStep, dirY * nStep);
      event->accept();
      return;
    }
    // Panning: record which arrow is down and let arrowPanTimer_'s tick combine whatever's
    // currently held — two arrows held together must pan diagonally, not stair-step (see the
    // timer's own comment, ctor).
    if (dirX < 0) panLeftHeld_ = true;
    else if (dirX > 0) panRightHeld_ = true;
    if (dirY < 0) panUpHeld_ = true;
    else if (dirY > 0) panDownHeld_ = true;
    panShiftHeld_ = mods & Qt::ShiftModifier;
    if (arrowPanTimer_ && !arrowPanTimer_->isActive()) arrowPanTimer_->start();
    event->accept();
  }

  // Clear the R-held flag when it (or focus) is released, so the Alt+R+←/→ chord doesn't stick
  // (and likewise the held-arrow pan flags, so a released key stops contributing to the tick).
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
    // End the Alt+Shift+O peek when the letter or any required modifier lifts.
    if (!event->isAutoRepeat() && canvas_->compareHoldOriginal() &&
        (event->key() == Qt::Key_O || event->key() == Qt::Key_Alt ||
         event->key() == Qt::Key_Shift || event->key() == Qt::Key_Meta ||
         event->key() == Qt::Key_Control)) {
      canvas_->setCompareHoldOriginal(false);
      refreshActions();   // restore the editing actions on release (parity with browser)
    }
    QMainWindow::keyReleaseEvent(event);
  }

  // Should this section button be on screen? Its action's own visibility, except in the
  // IMAGE cluster, which is empty-state aware: with no image only the labelled Open button
  // shows (browser #load-image-btn ↔ #image-actions). Both the per-action mirror and
  // refreshActions go through here, so whichever runs last agrees.
  bool MainWindow::sectionButtonVisible(QAction* act, QToolButton* btn) const {
    if (act && !act->isVisible()) return false;
    if (imageSection_ && btn && btn != openImageBtn_ && imageSection_->isAncestorOf(btn))
      return canvas_ && canvas_->hasImage();
    return true;
  }

  // Where the next dialog should grow from. Every action records its own origin when it
  // fires: its visible toolbar icon, else the menu row that was clicked, else nothing.
  // Clearing the anchor for a button-less action is the point — otherwise a dialog opened
  // from the menu bar or a shortcut flies out of whichever icon was used last.
  void MainWindow::bindRevealAnchors() {
    for (QAction* a : findChildren<QAction*>()) bindRevealAnchor(a);
  }

  // Bound the moment the action is CREATED, so this is its first triggered() slot and runs
  // before the handler. Bound later — after the handlers — it recorded the anchor only once
  // exec() had returned, i.e. after the dialog had come and gone, so every dialog flew out
  // of the icon used the time before (and the first one out of nowhere).
  void MainWindow::bindRevealAnchor(QAction* a) {
    if (!a || a->property("revealBound").toBool()) return;
    a->setProperty("revealBound", true);
    connect(a, &QAction::triggered, this, [this, a] {
      pop_.dialogAnchor = buttonForAction(a);   // resolved at trigger time; buttons come later
      pop_.dialogAnchorRect = (pop_.menuRowAction == a) ? pop_.menuRowRect : QRect();
    });
  }

  // The visible toolbar button that presents `act`, if any — used to anchor the theme
  // wipe at the icon the user actually pressed.
  QWidget* MainWindow::buttonForAction(QAction* act) const {
    if (!act) return nullptr;
    for (QToolButton* b : findChildren<QToolButton*>())
      if (b->defaultAction() == act && b->isVisible()) return b;
    return nullptr;
  }

}  // namespace stencil::gui
