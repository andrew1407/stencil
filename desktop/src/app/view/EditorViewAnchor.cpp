#include "MainWindow.hpp"
#include "EditorView.hpp"
#include "CanvasWidget.hpp"

#include <QScrollArea>
#include <algorithm>
#include <cmath>

// The zoom a rotate or flip keeps: the scale and the picture point centred in the viewport.
// Browser twin: ZoomPan.viewAnchor / restoreAnchor (js/core/zoom/pan.js).

namespace stencil::gui {

  // Unclamped: setZoom clamps it to the shared zoom range.
  double EditorView::fitScale() const {
    const QSize vp = w.scroll->viewport()->size();
    const double sx = double(vp.width()) / w.canvas->imageWidth();
    const double sy = double(vp.height()) / w.canvas->imageHeight();
    return std::min(sx, sy) * 0.95;
  }

  EditorView::ViewAnchor EditorView::viewAnchor() const {
    ViewAnchor a;
    if (!w.canvas->hasImage()) return a;
    a.scale = w.canvas->getScale();
    a.fit = std::abs(a.scale - fitScale()) < 1e-6;
    const QSize vp = w.scroll->viewport()->size();
    a.x = (vp.width() / 2.0 - w.canvas->x()) / a.scale;
    a.y = (vp.height() / 2.0 - w.canvas->y()) / a.scale;
    return a;
  }

  void EditorView::restoreAnchor(const ViewAnchor& a) {
    if (!w.canvas->hasImage()) return;
    if (a.fit || a.scale <= 0) {
      w.fitToWindow();
      return;
    }
    w.setZoom(a.scale);
    const QSize vp = w.scroll->viewport()->size();
    const double s = w.canvas->getScale();
    scrollTo(qRound(a.x * s - vp.width() / 2.0), qRound(a.y * s - vp.height() / 2.0));
  }

}  // namespace stencil::gui
