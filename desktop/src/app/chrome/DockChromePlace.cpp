#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "../../support/modal/modalReveal.hpp"   // support::motionReduced()
#include "mainWindowShared.hpp"
#include "mainWindowHelpers.hpp"
#include "SelectionPanel.hpp"
#include "guiHelpers.hpp"
#include "ChatDock.hpp"

#include <QVBoxLayout>
#include <QScrollArea>

// Docking and floating the chat, and placing the side panel's floating re-open chevron.

namespace stencil::gui {

  // The chat is the WINDOW's only dock: every side runs the full edge, outside the editor's
  // toolbars and panel, as the browser's fixed-position panel insets the whole page.
  void DockChrome::dockChatTo(Qt::DockWidgetArea area) {
    if (!w.chatDock) return;
    const auto place = [this, area] {
      w.addDockWidget(area, w.chatDock);
      w.chatDock->setFloating(false);
    };
    // A placement change reads as travel: out of the old edge, in at the new, dusting through
    // chatSurfaceFlight. Skipped when there is nothing on screen to move.
    const bool wasFloating = w.chatDock->isFloating();
    // Leaving a deliberate float — remember where it sat so the next one returns there.
    // Never the compact popover's tiny icon-anchored rect (toggleChatFloat guards alike).
    if (wasFloating && !w.chatCompactShowing()) chatFloatRect = w.chatDock->geometry();
    if (w.tearingDown || !w.chatDock->isVisible() || support::motionReduced()
        || (!wasFloating && w.dockWidgetArea(w.chatDock) == area)) {
      place();
      stopChatAnim();   // no animation follows to release the pinned extent — do it now
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
      chatAnim = startExtentSlide(&w, 0, full, 300, pinAndRaiseDust(pin, dustFx),
                                   [this] { stopChatAnim(); });
    };
    // Coming back from a FLOAT there is no edge to leave — it just slides in at the
    // side you picked (the browser plays its dock-in slide here too).
    if (wasFloating) {
      place();
      growIn();
      return;
    }
    const Qt::DockWidgetArea from = w.dockWidgetArea(w.chatDock);
    const bool horizFrom = from != Qt::TopDockWidgetArea && from != Qt::BottomDockWidgetArea;
    const int extent = horizFrom ? w.chatDock->width() : w.chatDock->height();
    if (extent > 80) chatRestoreExtent = extent;   // come back at the size it had
    const auto pinFrom = chatExtentPin(horizFrom);
    QPointer<gui::DisintegrateOverlay> outFx = chatSurfaceFlight(from, /*gather=*/false, 200, pinFrom, extent);
    chatAnim = startExtentSlide(&w, extent, 0, 200, pinAndRaiseDust(pinFrom, outFx),
                                 [this, place, growIn] {
      stopChatAnim();          // release the pinned extent before re-docking
      place();
      growIn();
    });
  }

  // QDockWidget::setFloating teleports with no animation of its own, so the flight is driven
  // here: chatSurfaceFlight's dust out of / into the icon, then dockChatTo's wasFloating leg.
  void DockChrome::toggleChatFloat() {
    if (!w.chatDock || w.tearingDown || !w.chatDock->isVisible()) return;
    stopChatAnim();
    QWidget* icon = w.buttonForAction(w.acts.chat);
    if (w.chatDock->isFloating()) {
      // The compact popover's own tiny, icon-anchored rect must never become the
      // remembered deliberate-float spot — only a shape the user actually adopted.
      if (!w.chatCompactShowing()) chatFloatRect = w.chatDock->geometry();
      setChatCompactPopover(false);   // a deliberate dock is never a popover shape
      support::dismissWindow(*w.chatDock, icon);   // hides at once; the ghost/dust flies
      // dockChatTo needs the dock VISIBLE to measure and dust the arrival; still floating, so it
      // re-hides behind the veil at once and never flashes the old floating shape back.
      w.chatDock->show();
      dockChatTo(chatCompactPrevArea);
      return;
    }
    const Qt::DockWidgetArea area = w.dockWidgetArea(w.chatDock);
    if (area != Qt::NoDockWidgetArea) chatCompactPrevArea = area;
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto pin = chatExtentPin(horiz);
    const int full = horiz ? w.chatDock->width() : w.chatDock->height();
    QPointer<gui::DisintegrateOverlay> outFx = chatSurfaceFlight(area, /*gather=*/false, 200, pin, full);
    const auto finishToFloat = [this, icon] {
      stopChatAnim();
      w.chatDock->setFloating(true);
      support::veilForReveal(*w.chatDock);   // setFloating maps it at once; same turn, so no frame shows
      // setFloating() alone derives the top-level placement from the collapsed 0-width docked
      // geometry, which lands off-screen - pin position AND size (browser chat/panel.js parity).
      w.chatDock->setGeometry(chatFloatRect.isValid() ? chatFloatRect : defaultChatFloatRect());
      support::revealWindow(*w.chatDock, icon);
    };
    if (!outFx) { finishToFloat(); return; }
    chatAnim = startExtentSlide(&w, full, 0, 200, pinAndRaiseDust(pin, outFx), finishToFloat);
  }

  void DockChrome::positionPanelReopenButton() {
    if (!w.tools.panelReopenBtn) return;
    // Flush to the EDITOR's right edge (a right-docked chat sits outside it) at scroll's top: canvas
    // and panel sit below the SAME dock stack, so the Y already matches.
    const int y = (w.scroll ? w.scroll->mapTo(&w, QPoint(0, 0)).y() : 0) + PANEL_TOGGLE_TOP;
    const int right = w.editor ? w.editor->mapTo(&w, QPoint(w.editor->width(), 0)).x() : w.width();
    w.tools.panelReopenBtn->move(right - w.tools.panelReopenBtn->width() - PANEL_TOGGLE_INSET, y);
  }

  void DockChrome::setCanvasRightInset(int right) {
    if (!w.centralLayout) return;
    QMargins m = w.centralLayout->contentsMargins();
    if (m.right() == right) return;
    m.setRight(right);
    w.centralLayout->setContentsMargins(m);
  }

  void DockChrome::updatePanelReopenButton() {
    if (!w.tools.panelReopenBtn) return;
    // isHidden(), not isVisible(): called from the constructor before the window is shown, where isVisible() is false for every child.
    const bool showBtn = w.selPanel && w.selPanel->isHidden() && !w.fs.active;
    w.tools.panelReopenBtn->setVisible(showBtn);
    // The WIDE margin only while the chevron floats over the canvas; CENTRAL_SIDE_MARGIN is the panel-open default.
    // A panel slide in flight owns it (setPanelShown), or each frame's resize would snap it back.
    if (!w.panelSlide.anim) setCanvasRightInset(showBtn ? CANVAS_RIGHT_MARGIN_COLLAPSED : CENTRAL_SIDE_MARGIN);
    if (showBtn) {
      // Back to 0°.
      spinIcon(w.tools.panelReopenBtn, "chevron-left", w.palette().color(QPalette::WindowText),
               PANEL_TOGGLE_GLYPH, 0, 0, 0);
      positionPanelReopenButton();
      w.tools.panelReopenBtn->raise();
    }
    positionChatEdge();
    positionPanelGrip();   // the grip is the reopen chevron's dual: shown while the panel is
  }

}  // namespace stencil::gui
