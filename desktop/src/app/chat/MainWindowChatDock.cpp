#include "MainWindow.hpp"
#include "../../support/guiHelpers.hpp"
#include "mainWindowShared.hpp"
#include "chatSlideClocks.hpp"
#include "PanelSlide.hpp"
#include "mainWindowHelpers.hpp"
#include "SelectionPanel.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "ControlsPill.hpp"
#include "ChatDock.hpp"
#include "tipContent.hpp"

#include <QLayout>
#include <QPalette>
#include <QTimer>

#include <algorithm>
#include <cmath>

// Docking and floating the chat and its compact popover, and the side panel: its show/hide slide,
// the floating re-open chevron and the Controls pill that pairs with it.

namespace stencil::gui {

  // QMainWindow overrides a dock's maximumWidth during layout, so pin min == max
  // (setFixedWidth) each frame and release the constraint at the end.
  void MainWindow::setPanelShown(bool show, bool animate) {
    if (!selPanel) return;
    if (support::motionReduced()) animate = false;   // `none`: no fold, no slide
    // A width read mid-slide is not one the user chose: only a settled panel updates the restore width.
    const bool settled = !panelSlide.anim;
    if (panelSlide.anim) { panelSlide.anim->stop(); panelSlide.anim->deleteLater(); panelSlide.anim = nullptr; }
    parts.dockChrome.releasePanelVeil();   // an interrupted flight must never leave the panel invisible
    if (!show && settled && selPanel->isVisible() && selPanel->width() > 120)
      panelSlide.restoreWidth = selPanel->width();
    const int full = panelSlide.restoreWidth > 120 ? panelSlide.restoreWidth : panelSlide.DEFAULT_WIDTH;
    // The two chevrons are different buttons but read as one toggle. Driven here so Alt+X and
    // the View menu turn it too, not only a click on the chevron.
    const PanelFoldClocks& clocks = panelFoldClocks();
    const int spinMs = animate ? (show ? clocks.panelInMs : clocks.panelOutMs) : 0;
    if (show) spinIcon(tools.panelReopenBtn, "chevron-left", palette().color(QPalette::WindowText),
                       PANEL_TOGGLE_GLYPH, 0, 180, spinMs);
    else selPanel->spinCollapseChevron(0, 180, spinMs);
    auto finish = [this, show, full] {
      parts.dockChrome.releasePanelVeil();
      selPanel->setMinimumWidth(panelSlide.MIN_WIDTH);
      selPanel->setMaximumWidth(QWIDGETSIZE_MAX);
      if (show) editor->resizeDocks({selPanel}, {full}, Qt::Horizontal);   // the layout's own record, or it drifts a pixel a cycle
      else selPanel->hide();
      panelSlide.anim = nullptr;
      parts.dockChrome.positionPanelGrip();
      parts.dockChrome.updatePanelReopenButton();
    };
    int from, to;
    QPointer<gui::DisintegrateOverlay> dustFx;
    if (show) {
      selPanel->show();
      // Photographed at the OPEN width; the slide starts from 1px, not 0 - at exactly zero Qt treats
      // the split's anchor pane as vacated and drops the panel out of the layout tree.
      if (animate) dustFx = parts.dockChrome.panelSurfaceFlight(/*gather=*/true, clocks.dustInMs, full);
      selPanel->setFixedWidth(1);
      from = 1; to = full;
    }
    else {
      from = selPanel->width() > 0 ? selPanel->width() : full;
      if (animate) dustFx = parts.dockChrome.panelSurfaceFlight(/*gather=*/false, clocks.dustOutMs, from);
      to = 0;
    }
    if (!animate) { selPanel->setFixedWidth(to); finish(); return; }
    // The canvas' right inset rides the width, so the canvas lands on the reopen chevron's band rather
    // than running past it and snapping back: shut, the inset stands in for the separator and the sliver.
    const bool right = editor->dockWidgetArea(selPanel) != Qt::LeftDockWidgetArea;
    const int shut = show ? from : to;
    const int shutInset = fs.active ? CENTRAL_SIDE_MARGIN
                                    : CANVAS_RIGHT_MARGIN_COLLAPSED - (right ? DOCK_SEPARATOR_PX + shut : 0);
    const auto insetAt = [shut, full, shutInset](int v) {
      const double open = std::clamp(double(v - shut) / std::max(1, full - shut), 0.0, 1.0);
      return int(std::lround(shutInset + (CENTRAL_SIDE_MARGIN - shutInset) * open));
    };
    panelSlide.anim = startExtentSlide(
        this, from, to, show ? clocks.panelInMs : clocks.panelOutMs,
        pinAndRaiseDust([this, insetAt](int v) {
          selPanel->setFixedWidth(v);
          parts.dockChrome.setCanvasRightInset(insetAt(v));
        }, dustFx),
        finish, panelSlideEase());
    // The start pose, laid out now: the first tick is a frame away, and the grab left the canvas at the open width.
    parts.dockChrome.setCanvasRightInset(insetAt(from));
    if (centralLayout) centralLayout->activate();
    if (QLayout* l = editor->layout()) l->activate();
    parts.dockChrome.positionPanelGrip();
  }

  // The chat icon's popover shape (browser chat/panel.js openCompact): the SAME dock, floated at its compact size next to the icon.
  void MainWindow::openChatCompact(QWidget* anchor) {
    if (!chatDock || tearingDown || !anchor) return;
    parts.dockChrome.stopChatAnim();  // a popover open mid-slide wins outright (setChatShown rule)
    // A chat already on screen in its FULL shape LEAVES through the animated path first, one window at a time.
    // Already pinned where this gesture wants it: raise and focus, no flight.
    if (chatCompactShowing() && chatDock->geometry() == parts.dockChrome.compactChatRect(anchor)) {
      chatDock->raise();
      chatDock->activateWindow();
      chatDock->focusInput();
      return;
    }
    // Anything else LEAVES first — a compact float that has to move included; teleporting it read as "the chat vanished".
    if (chatDock->isVisible()) {
      const int outMs = chatDock->isFloating() ? WINDOW_DISMISS_MS : chatCloseMs();
      parts.dockChrome.setChatCompactPopover(false);   // it is leaving; the next open re-establishes it
      parts.dockChrome.setChatShown(false, /*animate=*/true);
      QPointer<QWidget> pin(anchor);
      QTimer::singleShot(support::motionReduced() ? 0 : outMs, this, [this, pin] {
        if (pin) parts.dockChrome.openChatCompactNow(pin);
      });
      return;
    }
    parts.dockChrome.openChatCompactNow(anchor);
  }

  bool MainWindow::chatCompactShowing() const {
    return parts.dockChrome.chatCompactPopover && chatDock && chatDock->isFloating() &&
           chatDock->isVisible();
  }

  // Browser parity: `#toggle-controls .ic` spins half a turn on the fold's curve.
  void MainWindow::spinControlsPill(bool animate) {
    const qreal to = (acts.toolbars && !acts.toolbars->isChecked()) ? 180.0 : 0.0;
    if (tools.controlsPill) setTipBase(tools.controlsPill, to > 0.0 ? "Show controls" : "Hide controls");
    if (tools.pillSpinAnim) { tools.pillSpinAnim->stop(); tools.pillSpinAnim->deleteLater(); tools.pillSpinAnim = nullptr; }
    if (!animate || qFuzzyCompare(tools.pillChevronDeg + 1.0, to + 1.0)) {
      tools.pillChevronDeg = to;
      positionOverlayArrows();
      return;
    }
    const PanelFoldClocks& fold = panelFoldClocks();
    tools.pillSpinAnim = startExtentSlide(
        this, qRound(tools.pillChevronDeg), qRound(to), to > 0.0 ? fold.slideOutMs : fold.slideInMs,
        [this](int v) { tools.pillChevronDeg = v; positionOverlayArrows(); },
        [this] { tools.pillSpinAnim = nullptr; });
  }

  void MainWindow::positionOverlayArrows() {
    parts.dockChrome.syncToastInset();
    parts.dockChrome.positionChatEdge();
    if (tools.controlsPill) {
      // ONE glyph, turned: 0° is ↑, 180° is ↓; the pill paints it in the label's left padding so the button shrink-wraps the word.
      const QColor ic = palette().color(QPalette::WindowText);
      static_cast<ControlsPill*>(tools.controlsPill)->setChevron(tools.pillChevronDeg, ic, PILL_CHEVRON);
    }
    parts.dockChrome.updatePanelReopenButton();
  }

}  // namespace stencil::gui
