// Fullscreen's zoom hand-off: the canvas rides from the size it appeared at against the old viewport to
// its true size in the new one, at the user's own zoom, whichever way the switch went (browser
// fullscreen/layer.js captureView).
#include "MainWindow.hpp"
#include "EditorView.hpp"
#include "CanvasWidget.hpp"
#include "zoomPan.hpp"
#include "../../support/uiTimings.hpp"

#include <QScrollArea>
#include <QTimer>
#include <QVariantAnimation>

namespace stencil::gui {

  void EditorView::beginFullscreenZoom() {
    if (w.fs.zoomAnim) { w.fs.zoomAnim->stop(); w.fs.zoomAnim->deleteLater(); w.fs.zoomAnim = nullptr; }
    if (!w.canvas || !w.canvas->hasImage() || !w.scroll || !w.scroll->viewport()) {
      w.fs.zoomFromViewport = QSize();
      return;
    }
    w.fs.zoomFromViewport = w.scroll->viewport()->size();
    w.fs.zoomWaits = 0;
    QTimer::singleShot(0, &w, [this] { startFullscreenZoom(); });
  }

  void EditorView::startFullscreenZoom() {
    if (!w.fs.zoomFromViewport.isValid() || w.fs.zoomFromViewport.isEmpty()) return;
    if (!w.canvas || !w.canvas->hasImage() || !w.scroll || !w.scroll->viewport()) return;
    const QSize now = w.scroll->viewport()->size();
    if (now.isEmpty()) return;
    // showFullScreen()/showNormal() resize asynchronously on some platforms — wait for the new geometry, bounded.
    if (now == w.fs.zoomFromViewport) {
      if (++w.fs.zoomWaits > FullscreenController::ZOOM_WAIT_LIMIT) return;
      QTimer::singleShot(16, &w, [this] { startFullscreenZoom(); });
      return;
    }
    const auto ratio = FullscreenController::handoffRatio(w.fs.zoomFromViewport, now);
    w.fs.zoomFromViewport = QSize();   // consumed
    if (!ratio) return;
    const double target = w.canvas->getScale();
    const double start = core::clampScale(target * *ratio);
    if (std::abs(start - target) < 1e-4) return;

    auto* anim = new QVariantAnimation(&w);
    w.fs.zoomAnim = anim;
    anim->setDuration(support::motionMs(support::flipMotion().ms));
    anim->setEasingCurve(support::flipMotion().easing);
    anim->setStartValue(start);
    anim->setEndValue(target);
    QObject::connect(anim, &QVariantAnimation::valueChanged, &w,
                     [this](const QVariant& v) { if (w.canvas) w.canvas->setScale(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, &w, [this, target] {
      w.fs.zoomAnim = nullptr;
      w.setZoom(target);   // land exactly on the user's zoom and resync the combo
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui
