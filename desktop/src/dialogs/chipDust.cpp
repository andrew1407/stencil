// The shared chip-dust recipe: one cloud per row, and the height slide that goes with it.
#include "chipDust.hpp"
#include "openImageDialogParts.hpp"
#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QPixmap>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::gui {

  DisintegrateOverlay* chipCloud(QWidget* host, QWidget* w, const QPixmap& shot,
                                 DisintegrateOverlay::Sweep sweep) {
    int cols = 0, rows = 0;
    DisintegrateOverlay::dustGrid(w->size(), CHIP_CELL_PX,
                                  DisintegrateOverlay::SURFACE_MAX_CELLS, &cols, &rows);
    return DisintegrateOverlay::overPixmaps(shot, QPixmap(),
                                            QRect(w->mapTo(host, QPoint()), w->size()), host,
                                            sweep, cols, rows, CHIP_DUST_MS, CHIP_DUST_DRIFT);
  }

  void slideRowHeight(QObject* owner, QWidget* row, QVariantAnimation*& anim, bool show) {
    const int full = row->sizeHint().height();
    const int from = row->isVisible() && row->maximumHeight() < QWIDGETSIZE_MAX
                         ? row->maximumHeight()
                         : (show ? 0 : full);
    row->setVisible(true);
    const auto pin = [row](int h) {
      row->setMinimumHeight(h);   // a layout hands it its hint otherwise
      row->setMaximumHeight(h);
    };
    if (!anim) {
      anim = new QVariantAnimation(owner);
      anim->setDuration(OI_RESIZE_MS);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      QVariantAnimation* a = anim;
      QObject::connect(a, &QVariantAnimation::valueChanged, owner,
                       [pin](const QVariant& v) { pin(v.toInt()); });
      QObject::connect(a, &QVariantAnimation::finished, owner, [a, row] {
        const bool shown = a->endValue().toInt() > 0;
        row->setMinimumHeight(0);
        row->setMaximumHeight(shown ? QWIDGETSIZE_MAX : 0);
        row->setVisible(shown);
      });
    }
    anim->stop();
    anim->setStartValue(from);
    anim->setEndValue(show ? full : 0);
    // The END state into the layout LAST: setStartValue emits its value straight away, so
    // pinning before it left the line's old height in the layout for the refit to measure.
    pin(show ? full : 0);
    // Started ONE TURN LATER: start() emits its first value at once, which would put the
    // line's old height back into the layout before the caller's refit measures it — and
    // the window then settled a line short of its own content, both ways.
    QVariantAnimation* a = anim;
    QTimer::singleShot(0, a, [a] {
      if (a->state() != QAbstractAnimation::Running) a->start();
    });
  }

}  // namespace stencil::gui
