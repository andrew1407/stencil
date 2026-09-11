#include "mainWindow.hpp"
#include <QScrollArea>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "incognitoOverlay.hpp"
#include "zoomPan.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "projectsDialog.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QEasingCurve>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

// The zoom hand-off across a fullscreen switch.

namespace stencil::gui {

  // The window itself can't be animated between normal and fullscreen, but the
  // thing the user is actually looking at can: the canvas starts at the size it
  // APPEARED to be against the old viewport and rides to its true size, so entering
  // reads as the canvas stretching out and leaving as it minimising back. It always
  // LANDS on the scale the user picked — the zoom is preserved, matching the browser.
  void MainWindow::beginFullscreenZoom() {
    if (fsZoomAnim_) { fsZoomAnim_->stop(); fsZoomAnim_->deleteLater(); fsZoomAnim_ = nullptr; }
    if (!canvas_ || !canvas_->hasImage() || !scroll_ || !scroll_->viewport()) {
      fsZoomFromViewport_ = QSize();
      return;
    }
    fsZoomFromViewport_ = scroll_->viewport()->size();
    fsZoomWaits_ = 0;
    QTimer::singleShot(0, this, [this] { startFullscreenZoom(); });
  }

  void MainWindow::startFullscreenZoom() {
    if (!fsZoomFromViewport_.isValid() || fsZoomFromViewport_.isEmpty()) return;
    if (!canvas_ || !canvas_->hasImage() || !scroll_ || !scroll_->viewport()) return;
    const QSize now = scroll_->viewport()->size();
    if (now.isEmpty()) return;
    // showFullScreen()/showNormal() resize asynchronously on some platforms — wait for
    // the new geometry rather than animating against the old one. Bounded, so a window
    // manager that never resizes (an offscreen test) simply skips the animation.
    if (now == fsZoomFromViewport_) {
      if (++fsZoomWaits_ > 12) return;
      QTimer::singleShot(16, this, [this] { startFullscreenZoom(); });
      return;
    }
    const double ratio = std::min(double(fsZoomFromViewport_.width()) / now.width(),
                                  double(fsZoomFromViewport_.height()) / now.height());
    fsZoomFromViewport_ = QSize();   // consumed
    if (ratio < 0.05 || ratio > 20.0 || std::abs(ratio - 1.0) < 0.01) return;
    const double target = canvas_->scale();
    const double start = core::clampScale(target * ratio);
    if (std::abs(start - target) < 1e-4) return;

    auto* anim = new QVariantAnimation(this);
    fsZoomAnim_ = anim;
    // Long and hard-eased-out, matching FLIP_MS / FLIP_EASING in browser/js/ui/motion.js:
    // most of the distance early, coasting into the landing.
    anim->setDuration(560);
    anim->setEasingCurve(QEasingCurve::OutQuint);
    anim->setStartValue(start);
    anim->setEndValue(target);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { if (canvas_) canvas_->setScale(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, this, [this, target] {
      fsZoomAnim_ = nullptr;
      setZoom(target);   // land exactly on the user's zoom and resync the combo
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui
