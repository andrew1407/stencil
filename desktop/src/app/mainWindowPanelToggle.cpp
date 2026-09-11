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
      // ONE glyph, turned: 0° is ↑ (rows shown), 180° is ↓. spinControlsPill drives the
      // angle; the pill paints the chevron itself (ControlsPill) in the label's left
      // padding, so the button stays text-only and shrink-wraps the word (no icon-slot gap).
      const QColor ic = palette().color(QPalette::WindowText);
      static_cast<ControlsPill*>(controlsPill_)->setChevron(pillChevronDeg_, ic, kPillChevron);
    }
    updatePanelReopenButton();
  }

  // Browser parity: `#toggle-controls .ic` spins half a turn on the fold's curve rather
  // than blinking to the opposite chevron.
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
    // Flush against the WINDOW's own right edge, at the same height the panel's own header
    // sits at when open — simply scroll_'s own top edge: the canvas and the panel both sit
    // below the SAME dock stack (toolbars, "Selected Line:" bar, Image Size dock), so they
    // already land at an identical Y with nothing to recompute (browser parity: the
    // coord-panel's 36px collapsed rail stays in its own column, never floating over the picture).
    const int y = (scroll_ ? scroll_->mapTo(this, QPoint(0, 0)).y() : 0) + kPanelToggleTop;
    panelReopenBtn_->move(width() - panelReopenBtn_->width() - kPanelToggleInset, y);
  }

  void MainWindow::updatePanelReopenButton() {
    if (!panelReopenBtn_) return;
    // Only when the panel is fully hidden and we're not in fullscreen (which edge-hover-reveals it).
    // isHidden(), not isVisible() — called from the constructor before the window is ever
    // shown, where isVisible() is false for every child regardless of state (same trap the
    // windowState restore above this is careful to avoid): it showed this reopen chevron
    // OVER a panel that was actually already open, right from the app's first paint.
    const bool showBtn = selPanel_ && selPanel_->isHidden() && !fs_.active;
    panelReopenBtn_->setVisible(showBtn);
    // The canvas only needs the WIDE margin while the chevron is actually floating over it
    // (panel collapsed) — with the panel open, that room belongs to the dock gap instead.
    // kCentralSideMargin is the lean, panel-open default.
    if (centralLayout_) {
      QMargins m = centralLayout_->contentsMargins();
      const int wantRight = showBtn ? kCanvasRightMarginCollapsed : kCentralSideMargin;
      if (m.right() != wantRight) {
        m.setRight(wantRight);
        centralLayout_->setContentsMargins(m);
      }
    }
    if (showBtn) {
      // Back to 0° — any spin from the last click ended with the panel open, i.e. hidden.
      spinIcon(panelReopenBtn_, "chevron-left", palette().color(QPalette::WindowText),
               kPanelToggleGlyph, 0, 0, 0);
      positionPanelReopenButton();
      panelReopenBtn_->raise();
    }
    positionChatEdge();
    positionPanelGrip();   // the grip is the reopen chevron's dual: shown while the panel is
  }

}  // namespace stencil::gui
