#include "MainWindow.hpp"
#include <QScrollArea>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "zoomPan.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/ShimmerOverlay.hpp"

#include <QEasingCurve>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

// The zoom hand-off across a fullscreen switch.

namespace stencil::gui {

  // The canvas starts at the size it APPEARED to be against the old viewport and rides to its true size; the zoom is preserved (browser parity).
  void MainWindow::beginFullscreenZoom() {
    if (fs.zoomAnim) { fs.zoomAnim->stop(); fs.zoomAnim->deleteLater(); fs.zoomAnim = nullptr; }
    if (!canvas || !canvas->hasImage() || !scroll || !scroll->viewport()) {
      fs.zoomFromViewport = QSize();
      return;
    }
    fs.zoomFromViewport = scroll->viewport()->size();
    fs.zoomWaits = 0;
    QTimer::singleShot(0, this, [this] { startFullscreenZoom(); });
  }

  void MainWindow::startFullscreenZoom() {
    if (!fs.zoomFromViewport.isValid() || fs.zoomFromViewport.isEmpty()) return;
    if (!canvas || !canvas->hasImage() || !scroll || !scroll->viewport()) return;
    const QSize now = scroll->viewport()->size();
    if (now.isEmpty()) return;
    // showFullScreen()/showNormal() resize asynchronously on some platforms — wait for the new geometry, bounded.
    if (now == fs.zoomFromViewport) {
      if (++fs.zoomWaits > FullscreenController::ZOOM_WAIT_LIMIT) return;
      QTimer::singleShot(16, this, [this] { startFullscreenZoom(); });
      return;
    }
    const auto ratio = FullscreenController::handoffRatio(fs.zoomFromViewport, now);
    fs.zoomFromViewport = QSize();   // consumed
    if (!ratio) return;
    const double target = canvas->getScale();
    const double start = core::clampScale(target * *ratio);
    if (std::abs(start - target) < 1e-4) return;

    auto* anim = new QVariantAnimation(this);
    fs.zoomAnim = anim;
    // FLIP_MS / FLIP_EASING in browser/js/ui/motion.js.
    anim->setDuration(560);
    anim->setEasingCurve(QEasingCurve::OutQuint);
    anim->setStartValue(start);
    anim->setEndValue(target);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { if (canvas) canvas->setScale(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, this, [this, target] {
      fs.zoomAnim = nullptr;
      setZoom(target);   // land exactly on the user's zoom and resync the combo
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui
