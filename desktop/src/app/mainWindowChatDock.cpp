#include "mainWindow.hpp"
#include <QToolButton>
#include "../support/guiHelpers.hpp"
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
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
#include "../support/disintegrateOverlay.hpp"
#include "../support/modalChrome.hpp"

#include <QDockWidget>
#include <QPalette>

// Docking and floating the chat, and the side panel's own show/hide slide.

namespace stencil::gui {

  // Pin the chat to a side (title bar placement buttons, a drag dropped on a dock
  // zone, or the wasFloating leg of toggleChatFloat). The selection panel already
  // owns the right area, so docking there must SPLIT side-by-side — plain
  // addDockWidget stacks the two vertically, giving each a squashed half-height column.
  void MainWindow::dockChatTo(Qt::DockWidgetArea area) {
    if (!chatDock_) return;
    const auto place = [this, area] {
      // pinPanelWhileSharing (every call site below runs it first) already fixed the
      // panel's width, so the split lands on that instead of Qt's default even split.
      // Top/bottom must claim their own FULL-WIDTH row (Qt::Vertical): the top area
      // already holds the (usually hidden) selected-line dock, and the area's default
      // horizontal placement parked the chat beside its slot — a narrow, right-aligned
      // column instead of the browser's full-width band.
      if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea)
        addDockWidget(area, chatDock_, Qt::Vertical);
      else
        addDockWidget(area, chatDock_);
      chatDock_->setFloating(false);
      ensurePanelChatSplit();
    };
    // Moving between sides slides out of the old edge and back in at the new one —
    // the same extent slide the icon's open/close uses, so a placement change reads
    // as travel rather than a jump (and, via chatSurfaceFlight, dusts like every other
    // surface). Skipped when there is nothing on screen to move.
    const bool wasFloating = chatDock_->isFloating();
    // Leaving a deliberate float — remember where it sat so the next one returns there.
    // Never the compact popover's tiny icon-anchored rect (toggleChatFloat guards alike).
    if (wasFloating && !chatCompactShowing()) chatFloatRect_ = chatDock_->geometry();
    if (tearingDown_ || !chatDock_->isVisible() || support::motionReduced()
        || (!wasFloating && dockWidgetArea(chatDock_) == area)) {
      pinPanelWhileSharing(area);
      place();
      stopChatAnim();   // no animation follows to release the pin — do it now
      return;
    }
    stopChatAnim();
    const bool horizNew = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto growIn = [this, horizNew, area] {
      const auto pin = chatExtentPin(horizNew);
      const int full = chatRestoreExtent_ > 80 ? chatRestoreExtent_ : (horizNew ? 345 : 320);
      // chatSurfaceFlight leaves the dock pinned at 0 for a gather — nothing played
      // (no dust) still needs the explicit pin(0) it would otherwise have skipped.
      QPointer<gui::DisintegrateOverlay> dustFx = chatSurfaceFlight(area, /*gather=*/true, 300, pin, full);
      if (!dustFx) pin(0);
      chatAnim_ = startExtentSlide(this, 0, full, 300, pinAndRaiseDust(pin, dustFx),
                                   [this] { stopChatAnim(); });
    };
    // Coming back from a FLOAT there is no edge to leave — it just slides in at the
    // side you picked (the browser plays its dock-in slide here too).
    if (wasFloating) {
      pinPanelWhileSharing(area);
      place();
      growIn();
      return;
    }
    const Qt::DockWidgetArea from = dockWidgetArea(chatDock_);
    const bool horizFrom = from != Qt::TopDockWidgetArea && from != Qt::BottomDockWidgetArea;
    const int extent = horizFrom ? chatDock_->width() : chatDock_->height();
    if (extent > 80) chatRestoreExtent_ = extent;   // come back at the size it had
    const auto pinFrom = chatExtentPin(horizFrom);
    QPointer<gui::DisintegrateOverlay> outFx = chatSurfaceFlight(from, /*gather=*/false, 200, pinFrom, extent);
    chatAnim_ = startExtentSlide(this, extent, 0, 200, pinAndRaiseDust(pinFrom, outFx),
                                 [this, place, growIn, area] {
      stopChatAnim();          // release the pinned extent before re-docking
      pinPanelWhileSharing(area);
      place();
      growIn();
    });
  }

  // The title bar's Float button (browser chat-float-btn parity): QDockWidget's own
  // setFloating() just teleports the panel, with no animation of its own. Docked → float
  // leaves through the same dust a side switch does
  // (chatSurfaceFlight), then the floating shape flies out of the icon
  // (support::revealWindow — the very flight a torn-off/compact chat already uses).
  // Float → docked leaves through dismissWindow, then re-docks at the last area it
  // held; dockChatTo's own wasFloating branch plays that arrival's dust.
  void MainWindow::toggleChatFloat() {
    if (!chatDock_ || tearingDown_ || !chatDock_->isVisible()) return;
    stopChatAnim();
    QWidget* icon = buttonForAction(actChat_);
    if (chatDock_->isFloating()) {
      // The compact popover's own tiny, icon-anchored rect must never become the
      // remembered deliberate-float spot — only a shape the user actually adopted.
      if (!chatCompactShowing()) chatFloatRect_ = chatDock_->geometry();
      chatCompactPopover_ = false;   // a deliberate dock is never a popover shape
      support::dismissWindow(*chatDock_, icon);   // hides at once; the ghost/dust flies
      // dockChatTo needs the dock VISIBLE to measure and dust the arrival (setChatShown's
      // own open does the same re-show first) — still floating, so it re-hides behind
      // the veil at once and this never actually flashes the old floating shape back.
      chatDock_->show();
      dockChatTo(chatCompactPrevArea_);
      return;
    }
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
    if (area != Qt::NoDockWidgetArea) chatCompactPrevArea_ = area;
    // Same reason as setChatShown/dockChatTo: if the panel shares this side, pin its
    // width for the whole closing slide so it never fights the chat's own collapse.
    pinPanelWhileSharing(area);
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto pin = chatExtentPin(horiz);
    const int full = horiz ? chatDock_->width() : chatDock_->height();
    QPointer<gui::DisintegrateOverlay> outFx = chatSurfaceFlight(area, /*gather=*/false, 200, pin, full);
    const auto finishToFloat = [this, icon] {
      stopChatAnim();
      chatDock_->setFloating(true);
      // setFloating() alone derives the top-level placement from the just-collapsed
      // 0-width docked geometry, which lands off-screen — pin position AND size.
      // Remembered for the session once the user moves it (browser chatPanel.js parity).
      chatDock_->setGeometry(chatFloatRect_.isValid() ? chatFloatRect_ : defaultChatFloatRect());
      support::revealWindow(*chatDock_, icon);
    };
    if (!outFx) { finishToFloat(); return; }
    chatAnim_ = startExtentSlide(this, full, 0, 200, pinAndRaiseDust(pin, outFx), finishToFloat);
  }

  // Show = slide the dock open to full width; hide = slide it to 0 then fully hide it (the canvas
  // fills the freed space) and reveal the floating right-edge re-open chevron. QMainWindow overrides
  // a dock's maximumWidth during its own layout passes, so we pin min==max (setFixedWidth) each frame
  // to force the width, then release the constraint at the end.
  void MainWindow::setPanelShown(bool show, bool animate) {
    if (!selPanel_) return;
    if (panelAnim_) { panelAnim_->stop(); panelAnim_->deleteLater(); panelAnim_ = nullptr; }
    releasePanelVeil();   // an interrupted flight must never leave the panel invisible
    const int full = panelRestoreWidth_ > 120 ? panelRestoreWidth_ : PANEL_DEFAULT_WIDTH;
    if (!show && selPanel_->isVisible() && selPanel_->width() > 120)
      panelRestoreWidth_ = selPanel_->width();
    // The two chevrons are different buttons in different places, but they read as one
    // toggle: whichever is on screen turns half a revolution with the slide and lands on
    // the glyph the other one takes over with. Driven here, so Alt+X and the View menu
    // turn it too — not just a click on the chevron itself.
    const int spinMs = animate ? (show ? FOLD_MS : FOLD_OUT_MS) : 0;
    if (show) spinIcon(panelReopenBtn_, "chevron-left", palette().color(QPalette::WindowText),
                       PANEL_TOGGLE_GLYPH, 0, 180, spinMs);
    else selPanel_->spinCollapseChevron(0, 180, spinMs);
    auto finish = [this, show] {
      releasePanelVeil();
      selPanel_->setMinimumWidth(PANEL_MIN_WIDTH);
      selPanel_->setMaximumWidth(QWIDGETSIZE_MAX);
      if (!show) selPanel_->hide();
      panelAnim_ = nullptr;
      updatePanelReopenButton();
    };
    int from, to;
    QPointer<gui::DisintegrateOverlay> dustFx;
    if (show) {
      selPanel_->show();
      // The chat may already be docked at this side from while the panel was hidden,
      // stacked rather than split — re-establish the split now the panel is back.
      ensurePanelChatSplit();
      // The flight is photographed at the OPEN width (it leaves the panel pinned there);
      // 1px, not 0, is what the slide then starts from: at exactly zero Qt treats the
      // split's anchor pane as vacated and drops the panel out of the layout tree.
      if (animate) dustFx = panelSurfaceFlight(/*gather=*/true, FOLD_DUST_IN_MS, full);
      selPanel_->setFixedWidth(1);
      from = 1; to = full;
    }
    else {
      from = selPanel_->width() > 0 ? selPanel_->width() : full;
      if (animate) dustFx = panelSurfaceFlight(/*gather=*/false, FOLD_DUST_OUT_MS, from);
      to = 0;
    }
    if (!animate) { selPanel_->setFixedWidth(to); finish(); return; }
    panelAnim_ = startExtentSlide(
        this, from, to, show ? FOLD_MS : FOLD_OUT_MS,
        pinAndRaiseDust([this](int v) { selPanel_->setFixedWidth(v); }, dustFx),
        finish);
  }

}  // namespace stencil::gui
