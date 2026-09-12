#include "mainWindow.hpp"
#include <QVBoxLayout>
#include "selectionPanel.hpp"
#include <QScrollArea>
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "controlsPill.hpp"

#include <QPalette>

// The side panel's floating re-open chevron and the Controls pill that pairs with it.

namespace stencil::gui {

  void MainWindow::positionOverlayArrows() {
    syncToastInset();
    positionChatEdge();
    if (controlsPill_) {
      // ONE glyph, turned: 0° is ↑, 180° is ↓; the pill paints it in the label's left padding so the button shrink-wraps the word.
      const QColor ic = palette().color(QPalette::WindowText);
      static_cast<ControlsPill*>(controlsPill_)->setChevron(pillChevronDeg_, ic, kPillChevron);
    }
    updatePanelReopenButton();
  }

  // Browser parity: `#toggle-controls .ic` spins half a turn on the fold's curve.
  void MainWindow::spinControlsPill(bool animate) {
    const qreal to = (actToolbars_ && !actToolbars_->isChecked()) ? 180.0 : 0.0;
    if (pillSpinAnim_) { pillSpinAnim_->stop(); pillSpinAnim_->deleteLater(); pillSpinAnim_ = nullptr; }
    if (!animate || qFuzzyCompare(pillChevronDeg_ + 1.0, to + 1.0)) {
      pillChevronDeg_ = to;
      positionOverlayArrows();
      return;
    }
    pillSpinAnim_ = startExtentSlide(
        this, qRound(pillChevronDeg_), qRound(to), to > 0.0 ? kFoldOutMs : kFoldMs,
        [this](int v) { pillChevronDeg_ = v; positionOverlayArrows(); },
        [this] { pillSpinAnim_ = nullptr; });
  }

  void MainWindow::positionPanelReopenButton() {
    if (!panelReopenBtn_) return;
    // Flush to the WINDOW's right edge at scroll_'s top: canvas and panel sit below the SAME dock stack, so the Y already matches.
    const int y = (scroll_ ? scroll_->mapTo(this, QPoint(0, 0)).y() : 0) + kPanelToggleTop;
    panelReopenBtn_->move(width() - panelReopenBtn_->width() - kPanelToggleInset, y);
  }

  void MainWindow::updatePanelReopenButton() {
    if (!panelReopenBtn_) return;
    // isHidden(), not isVisible(): called from the constructor before the window is shown, where isVisible() is false for every child.
    const bool showBtn = selPanel_ && selPanel_->isHidden() && !fs_.active;
    panelReopenBtn_->setVisible(showBtn);
    // The WIDE margin only while the chevron floats over the canvas; kCentralSideMargin is the panel-open default.
    if (centralLayout_) {
      QMargins m = centralLayout_->contentsMargins();
      const int wantRight = showBtn ? kCanvasRightMarginCollapsed : kCentralSideMargin;
      if (m.right() != wantRight) {
        m.setRight(wantRight);
        centralLayout_->setContentsMargins(m);
      }
    }
    if (showBtn) {
      // Back to 0°.
      spinIcon(panelReopenBtn_, "chevron-left", palette().color(QPalette::WindowText),
               kPanelToggleGlyph, 0, 0, 0);
      positionPanelReopenButton();
      panelReopenBtn_->raise();
    }
    positionChatEdge();
    positionPanelGrip();   // the grip is the reopen chevron's dual: shown while the panel is
  }

}  // namespace stencil::gui
