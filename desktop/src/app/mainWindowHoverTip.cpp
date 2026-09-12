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

  // Debounced by target (browser tooltip.js scheduleShow); `immediate` skips the wait.
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
    // A different target: hide only if one is actually ON SCREEN, else just drop the timer.
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
      // Same 200 ms wake-up as every other tooltip (main.cpp SH_ToolTip_WakeUpDelay).
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

  // Drops the pending reveal too — an abandoned target must not pop in late.
  void MainWindow::hideHoverTooltip() {
    if (hoverTooltipTimer_) hoverTooltipTimer_->stop();
    hoverPendingKey_.clear();
    hoverPendingReveal_ = nullptr;
    hoverShownKey_.clear();
    tooltip_->hide();
  }

}  // namespace stencil::gui
