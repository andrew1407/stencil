#include "MainWindow.hpp"
#include <QToolButton>
#include "../support/guiHelpers.hpp"
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
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
#include "../support/DisintegrateOverlay.hpp"
#include "../support/modalChrome.hpp"

#include <QDockWidget>
#include <QPalette>

// Docking and floating the chat, and the side panel's own show/hide slide.

namespace stencil::gui {

  // The selection panel owns the right area, so docking there must SPLIT side-by-side - plain
  // addDockWidget stacks them into two squashed half-height columns.
  void MainWindow::dockChatTo(Qt::DockWidgetArea area) {
    if (!chatDock_) return;
    const auto place = [this, area] {
      // pinPanelWhileSharing has already fixed the panel's width, so the split lands on that, not
      // Qt's even one. Top/bottom need Qt::Vertical for a FULL-WIDTH row.
      if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea)
        addDockWidget(area, chatDock_, Qt::Vertical);
      else
        addDockWidget(area, chatDock_);
      chatDock_->setFloating(false);
      ensurePanelChatSplit();
    };
    // A placement change reads as travel: out of the old edge, in at the new, dusting through
    // chatSurfaceFlight. Skipped when there is nothing on screen to move.
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

  // QDockWidget::setFloating teleports with no animation of its own, so the flight is driven
  // here: chatSurfaceFlight's dust out of / into the icon, then dockChatTo's wasFloating leg.
  void MainWindow::toggleChatFloat() {
    if (!chatDock_ || tearingDown_ || !chatDock_->isVisible()) return;
    stopChatAnim();
    QWidget* icon = buttonForAction(actChat_);
    if (chatDock_->isFloating()) {
      // The compact popover's own tiny, icon-anchored rect must never become the
      // remembered deliberate-float spot — only a shape the user actually adopted.
      if (!chatCompactShowing()) chatFloatRect_ = chatDock_->geometry();
      setChatCompactPopover(false);   // a deliberate dock is never a popover shape
      support::dismissWindow(*chatDock_, icon);   // hides at once; the ghost/dust flies
      // dockChatTo needs the dock VISIBLE to measure and dust the arrival; still floating, so it
      // re-hides behind the veil at once and never flashes the old floating shape back.
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
      // setFloating() alone derives the top-level placement from the collapsed 0-width docked
      // geometry, which lands off-screen - pin position AND size (browser chatPanel.js parity).
      chatDock_->setGeometry(chatFloatRect_.isValid() ? chatFloatRect_ : defaultChatFloatRect());
      support::revealWindow(*chatDock_, icon);
    };
    if (!outFx) { finishToFloat(); return; }
    chatAnim_ = startExtentSlide(this, full, 0, 200, pinAndRaiseDust(pin, outFx), finishToFloat);
  }

  // QMainWindow overrides a dock's maximumWidth during layout, so pin min == max
  // (setFixedWidth) each frame and release the constraint at the end.
  void MainWindow::setPanelShown(bool show, bool animate) {
    if (!selPanel_) return;
    if (panelAnim_) { panelAnim_->stop(); panelAnim_->deleteLater(); panelAnim_ = nullptr; }
    releasePanelVeil();   // an interrupted flight must never leave the panel invisible
    const int full = panelRestoreWidth_ > 120 ? panelRestoreWidth_ : PANEL_DEFAULT_WIDTH;
    if (!show && selPanel_->isVisible() && selPanel_->width() > 120)
      panelRestoreWidth_ = selPanel_->width();
    // The two chevrons are different buttons but read as one toggle. Driven here so Alt+X and
    // the View menu turn it too, not only a click on the chevron.
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
      // Photographed at the OPEN width; the slide starts from 1px, not 0 - at exactly zero Qt treats
      // the split's anchor pane as vacated and drops the panel out of the layout tree.
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
