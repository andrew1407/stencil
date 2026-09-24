#include "MainWindow.hpp"
#include "../../support/modal/modalReveal.hpp"   // support::motionReduced()
#include <QToolButton>
#include "../../support/guiHelpers.hpp"
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
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QDockWidget>
#include <QPalette>

// Docking and floating the chat, and the side panel's own show/hide slide.

namespace stencil::gui {

  // The selection panel owns the right area, so docking there must SPLIT side-by-side - plain
  // addDockWidget stacks them into two squashed half-height columns.
  void MainWindow::dockChatTo(Qt::DockWidgetArea area) {
    if (!chatDock) return;
    const auto place = [this, area] {
      // pinPanelWhileSharing has already fixed the panel's width, so the split lands on that, not
      // Qt's even one. Top/bottom need Qt::Vertical for a FULL-WIDTH row.
      if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea)
        addDockWidget(area, chatDock, Qt::Vertical);
      else
        addDockWidget(area, chatDock);
      chatDock->setFloating(false);
      ensurePanelChatSplit();
    };
    // A placement change reads as travel: out of the old edge, in at the new, dusting through
    // chatSurfaceFlight. Skipped when there is nothing on screen to move.
    const bool wasFloating = chatDock->isFloating();
    // Leaving a deliberate float — remember where it sat so the next one returns there.
    // Never the compact popover's tiny icon-anchored rect (toggleChatFloat guards alike).
    if (wasFloating && !chatCompactShowing()) chatFloatRect = chatDock->geometry();
    if (tearingDown || !chatDock->isVisible() || support::motionReduced()
        || (!wasFloating && dockWidgetArea(chatDock) == area)) {
      pinPanelWhileSharing(area);
      place();
      stopChatAnim();   // no animation follows to release the pin — do it now
      return;
    }
    stopChatAnim();
    const bool horizNew = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto growIn = [this, horizNew, area] {
      const auto pin = chatExtentPin(horizNew);
      const int full = chatOpenExtent(chatRestoreExtent, horizNew);
      // chatSurfaceFlight leaves the dock pinned at 0 for a gather — nothing played
      // (no dust) still needs the explicit pin(0) it would otherwise have skipped.
      QPointer<gui::DisintegrateOverlay> dustFx = chatSurfaceFlight(area, /*gather=*/true, 300, pin, full);
      if (!dustFx) pin(0);
      chatAnim = startExtentSlide(this, 0, full, 300, pinAndRaiseDust(pin, dustFx),
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
    const Qt::DockWidgetArea from = dockWidgetArea(chatDock);
    const bool horizFrom = from != Qt::TopDockWidgetArea && from != Qt::BottomDockWidgetArea;
    const int extent = horizFrom ? chatDock->width() : chatDock->height();
    if (extent > 80) chatRestoreExtent = extent;   // come back at the size it had
    const auto pinFrom = chatExtentPin(horizFrom);
    QPointer<gui::DisintegrateOverlay> outFx = chatSurfaceFlight(from, /*gather=*/false, 200, pinFrom, extent);
    chatAnim = startExtentSlide(this, extent, 0, 200, pinAndRaiseDust(pinFrom, outFx),
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
    if (!chatDock || tearingDown || !chatDock->isVisible()) return;
    stopChatAnim();
    QWidget* icon = buttonForAction(actChat);
    if (chatDock->isFloating()) {
      // The compact popover's own tiny, icon-anchored rect must never become the
      // remembered deliberate-float spot — only a shape the user actually adopted.
      if (!chatCompactShowing()) chatFloatRect = chatDock->geometry();
      setChatCompactPopover(false);   // a deliberate dock is never a popover shape
      support::dismissWindow(*chatDock, icon);   // hides at once; the ghost/dust flies
      // dockChatTo needs the dock VISIBLE to measure and dust the arrival; still floating, so it
      // re-hides behind the veil at once and never flashes the old floating shape back.
      chatDock->show();
      dockChatTo(chatCompactPrevArea);
      return;
    }
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock);
    if (area != Qt::NoDockWidgetArea) chatCompactPrevArea = area;
    // Same reason as setChatShown/dockChatTo: if the panel shares this side, pin its
    // width for the whole closing slide so it never fights the chat's own collapse.
    pinPanelWhileSharing(area);
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto pin = chatExtentPin(horiz);
    const int full = horiz ? chatDock->width() : chatDock->height();
    QPointer<gui::DisintegrateOverlay> outFx = chatSurfaceFlight(area, /*gather=*/false, 200, pin, full);
    const auto finishToFloat = [this, icon] {
      stopChatAnim();
      chatDock->setFloating(true);
      // setFloating() alone derives the top-level placement from the collapsed 0-width docked
      // geometry, which lands off-screen - pin position AND size (browser chat/panel.js parity).
      chatDock->setGeometry(chatFloatRect.isValid() ? chatFloatRect : defaultChatFloatRect());
      support::revealWindow(*chatDock, icon);
    };
    if (!outFx) { finishToFloat(); return; }
    chatAnim = startExtentSlide(this, full, 0, 200, pinAndRaiseDust(pin, outFx), finishToFloat);
  }

  // QMainWindow overrides a dock's maximumWidth during layout, so pin min == max
  // (setFixedWidth) each frame and release the constraint at the end.
  void MainWindow::setPanelShown(bool show, bool animate) {
    if (!selPanel) return;
    if (support::motionReduced()) animate = false;   // `none`: no fold, no slide
    // A width read mid-slide is not one the user chose: only a settled panel updates the restore width.
    const bool settled = !panelAnim;
    if (panelAnim) { panelAnim->stop(); panelAnim->deleteLater(); panelAnim = nullptr; }
    releasePanelVeil();   // an interrupted flight must never leave the panel invisible
    const int full = panelRestoreWidth > 120 ? panelRestoreWidth : PANEL_DEFAULT_WIDTH;
    if (!show && settled && selPanel->isVisible() && selPanel->width() > 120)
      panelRestoreWidth = selPanel->width();
    // The two chevrons are different buttons but read as one toggle. Driven here so Alt+X and
    // the View menu turn it too, not only a click on the chevron.
    const int spinMs = animate ? (show ? FOLD_MS : FOLD_OUT_MS) : 0;
    if (show) spinIcon(panelReopenBtn, "chevron-left", palette().color(QPalette::WindowText),
                       PANEL_TOGGLE_GLYPH, 0, 180, spinMs);
    else selPanel->spinCollapseChevron(0, 180, spinMs);
    auto finish = [this, show, full] {
      releasePanelVeil();
      selPanel->setMinimumWidth(PANEL_MIN_WIDTH);
      selPanel->setMaximumWidth(QWIDGETSIZE_MAX);
      if (show) resizeDocks({selPanel}, {full}, Qt::Horizontal);   // the layout's own record, or it drifts a pixel a cycle
      else selPanel->hide();
      panelAnim = nullptr;
      positionPanelGrip();
      updatePanelReopenButton();
    };
    int from, to;
    QPointer<gui::DisintegrateOverlay> dustFx;
    if (show) {
      selPanel->show();
      // The chat may already be docked at this side from while the panel was hidden,
      // stacked rather than split — re-establish the split now the panel is back.
      ensurePanelChatSplit();
      // Photographed at the OPEN width; the slide starts from 1px, not 0 - at exactly zero Qt treats
      // the split's anchor pane as vacated and drops the panel out of the layout tree.
      if (animate) dustFx = panelSurfaceFlight(/*gather=*/true, FOLD_DUST_IN_MS, full);
      selPanel->setFixedWidth(1);
      from = 1; to = full;
    }
    else {
      from = selPanel->width() > 0 ? selPanel->width() : full;
      if (animate) dustFx = panelSurfaceFlight(/*gather=*/false, FOLD_DUST_OUT_MS, from);
      to = 0;
    }
    if (!animate) { selPanel->setFixedWidth(to); finish(); return; }
    panelAnim = startExtentSlide(
        this, from, to, show ? FOLD_MS : FOLD_OUT_MS,
        pinAndRaiseDust([this](int v) { selPanel->setFixedWidth(v); }, dustFx),
        finish);
    positionPanelGrip();
  }

}  // namespace stencil::gui
