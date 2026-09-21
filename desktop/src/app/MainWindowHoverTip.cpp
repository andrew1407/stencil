#include "MainWindow.hpp"
#include "CanvasTooltip.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "../support/DisintegrateOverlay.hpp"

#include <QTimer>

// The canvas hover tooltip's reveal debounce.

namespace stencil::gui {

  // Debounced by target (browser tooltip.js scheduleShow); `immediate` skips the wait.
  void MainWindow::scheduleHoverShow(const QString& key, std::function<void()> revealFn,
                                     bool immediate) {
    if (immediate) {
      if (hoverTooltipTimer) hoverTooltipTimer->stop();
      hoverPendingKey.clear();
      hoverPendingReveal = nullptr;
      hoverShownKey = key;
      revealFn();
      return;
    }
    if (hoverShownKey == key) { revealFn(); return; }
    if (hoverPendingKey == key) { hoverPendingReveal = std::move(revealFn); return; }
    // A different target: hide only if one is actually ON SCREEN, else just drop the timer.
    if (!hoverShownKey.isEmpty()) {
      hoverShownKey.clear();
      tooltip->hide();
    } else if (hoverTooltipTimer) {
      hoverTooltipTimer->stop();
    }
    hoverPendingKey = key;
    hoverPendingReveal = std::move(revealFn);
    if (!hoverTooltipTimer) {
      hoverTooltipTimer = new QTimer(this);
      hoverTooltipTimer->setSingleShot(true);
      // Same 200 ms wake-up as every other tooltip (main.cpp SH_ToolTip_WakeUpDelay).
      hoverTooltipTimer->setInterval(200);
      connect(hoverTooltipTimer, &QTimer::timeout, this, [this] {
        hoverShownKey = hoverPendingKey;
        hoverPendingKey.clear();
        auto fn = std::move(hoverPendingReveal);
        hoverPendingReveal = nullptr;
        if (fn) fn();
      });
    }
    hoverTooltipTimer->start();
  }

  // Drops the pending reveal too — an abandoned target must not pop in late.
  void MainWindow::hideHoverTooltip() {
    if (hoverTooltipTimer) hoverTooltipTimer->stop();
    hoverPendingKey.clear();
    hoverPendingReveal = nullptr;
    hoverShownKey.clear();
    tooltip->hide();
  }

}  // namespace stencil::gui
