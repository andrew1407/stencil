#include "mainWindow.hpp"
#include "canvasTooltip.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "projectsDialog.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "../support/disintegrateOverlay.hpp"

#include <QTimer>

// The canvas hover tooltip's reveal debounce.

namespace stencil::gui {

  // Debounces the tooltip reveal by the target hovered (browser: tooltip.js
  // scheduleShow); `immediate` (refreshHoverForModifiers) skips the wait outright.
  void MainWindow::scheduleHoverShow(const QString& key, std::function<void()> revealFn,
                                     bool immediate) {
    if (immediate) {
      if (hoverTooltipTimer_) hoverTooltipTimer_->stop();
      hoverPendingKey_.clear();
      hoverPendingReveal_ = nullptr;
      hoverShownKey_ = key;
      revealFn();
      return;
    }
    if (hoverShownKey_ == key) { revealFn(); return; }
    if (hoverPendingKey_ == key) { hoverPendingReveal_ = std::move(revealFn); return; }
    // A different target: if one is actually ON SCREEN, take it down — otherwise
    // nothing has appeared yet, so there's only a timer to drop, not a hide.
    if (!hoverShownKey_.isEmpty()) {
      hoverShownKey_.clear();
      tooltip_->hide();
    } else if (hoverTooltipTimer_) {
      hoverTooltipTimer_->stop();
    }
    hoverPendingKey_ = key;
    hoverPendingReveal_ = std::move(revealFn);
    if (!hoverTooltipTimer_) {
      hoverTooltipTimer_ = new QTimer(this);
      hoverTooltipTimer_->setSingleShot(true);
      // Same wake-up delay as every other tooltip in the app (main.cpp pins the
      // toolbar/menu one's SH_ToolTip_WakeUpDelay at the same 200 ms).
      hoverTooltipTimer_->setInterval(200);
      connect(hoverTooltipTimer_, &QTimer::timeout, this, [this] {
        hoverShownKey_ = hoverPendingKey_;
        hoverPendingKey_.clear();
        auto fn = std::move(hoverPendingReveal_);
        hoverPendingReveal_ = nullptr;
        if (fn) fn();
      });
    }
    hoverTooltipTimer_->start();
  }

  // Drops the tooltip AND any pending reveal for it — a target abandoned mid-wait must
  // not pop in late, describing whatever the cursor has since moved on to.
  void MainWindow::hideHoverTooltip() {
    if (hoverTooltipTimer_) hoverTooltipTimer_->stop();
    hoverPendingKey_.clear();
    hoverPendingReveal_ = nullptr;
    hoverShownKey_.clear();
    tooltip_->hide();
  }

}  // namespace stencil::gui
