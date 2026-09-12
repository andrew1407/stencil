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
