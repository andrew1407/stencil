#include "DisintegrateOverlay.hpp"

namespace stencil::gui {

  // `host` must outlive the play. nullptr = nothing worth animating, just remove it. 0 cols/rows/ms
  // keep the defaults; `ink` lifts the grains towards the victim's text colour.
  DisintegrateOverlay* DisintegrateOverlay::over(QWidget* victim, QWidget* host, Sweep sweep,
                                                 int cols, int rows, int ms, const QColor& ink) {
    if (!support::isDustAllowed()) return nullptr;
    if (!victim || !host || !victim->isVisible()) return nullptr;
    if (victim->width() < 8 || victim->height() < 8) return nullptr;
    const QPixmap snap = victim->grab();
    if (snap.isNull()) return nullptr;
    // Parented to the host: the victim is about to be destroyed.
    const QPoint at = victim->mapTo(host, QPoint(0, 0));
    auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
    fx->ink = ink;
    fx->sweep = sweep;
    if (sweep != Sweep::ROWS) fx->sizeGridForDust(victim->size());
    if (cols > 0) fx->cols = cols;
    if (rows > 0) fx->rows = rows;
    fx->setGeometry(QRect(at, victim->size()));
    fx->show();
    fx->raise();
    // Again after the relayout removing the victim triggers — it restacks the host's children.
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms > 0 ? ms : DUST_MS);
    return fx;
  }


  // For a delegate-painted row with no widget of its own; `rect` in `source` coordinates. A mass
  // removal divides `dustCells` between its overlays - the per-frame cost is the SUM (scatterGridFor).
  DisintegrateOverlay* DisintegrateOverlay::overRect(QWidget* source, const QRect& rect,
                                                     QWidget* host, Sweep sweep, bool dust,
                                                     int dustCells, int ms, const QColor& ink,
                                                     const QPixmap& shot) {
    if (!support::isDustAllowed()) return nullptr;
    if (!source || !host || !source->isVisible()) return nullptr;
    if (rect.width() < 8 || rect.height() < 8) return nullptr;
    const QPixmap snap = shot.isNull() ? source->grab(rect) : shot;
    if (snap.isNull()) return nullptr;
    const QPoint at = source->mapTo(host, rect.topLeft());
    auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
    fx->ink = ink;
    fx->sweep = sweep;
    if (dust || sweep != Sweep::ROWS) fx->sizeGridForDust(rect.size(), dustCells);
    fx->setGeometry(QRect(at, rect.size()));
    fx->show();
    fx->raise();
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms > 0 ? ms : DUST_MS);
    return fx;
  }


  // For a style-painted glyph (a checkbox indicator): `particles` comes and goes, `base` is painted
  // opaque underneath. `at` in host coordinates; `pad` widens the layer without moving the picture.
  DisintegrateOverlay* DisintegrateOverlay::overPixmaps(const QPixmap& particles,
                                                        const QPixmap& base, const QRect& at,
                                                        QWidget* host, Sweep sweep, int cols,
                                                        int rows, int ms, double spread,
                                                        int pad, const QString& name) {
    if (!support::isDustAllowed()) return nullptr;
    if (!host || particles.isNull() || at.width() < 2 || at.height() < 2) return nullptr;
    auto* fx = new DisintegrateOverlay(host, particles);
    if (!name.isEmpty()) fx->setObjectName(name);
    fx->base = base;
    fx->sweep = sweep;
    fx->cols = std::max(1, cols);
    fx->rows = std::max(1, rows);
    fx->spread = spread;
    fx->pad = std::max(0, pad);
    fx->setGeometry(at.adjusted(-fx->pad, -fx->pad, fx->pad, fx->pad));
    fx->show();
    fx->raise();
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms);
    return fx;
  }


  // All GLOBAL. A layer smaller than this crops the cloud.
  QRect DisintegrateOverlay::surfaceLayerRect(const QRect& pictureGlobal,
                                              const QPoint& targetGlobal) {
    QRect need = pictureGlobal;
    need |= QRect(targetGlobal, QSize(1, 1));
    return need.adjusted(-SURFACE_PAD_PX, -SURFACE_PAD_PX, SURFACE_PAD_PX, SURFACE_PAD_PX);
  }


  // `picture`/`target` in HOST coordinates. `escapeHost`: a top-level surface's cloud may leave the
  // host; `alwaysEscape`: a window floating ABOVE the host would hide a child layer's motes.
  DisintegrateOverlay* DisintegrateOverlay::overSurface(const QPixmap& snap,
                                                        const QRect& picture, QWidget* host,
                                                        const QPoint& target, bool gather,
                                                        int ms, const QColor& ink, int maxCells,
                                                        bool escapeHost, bool alwaysEscape) {
    if (!support::isDustAllowed()) return nullptr;
    if (!host || snap.isNull() || picture.width() < 8 || picture.height() < 8) return nullptr;
    // NOT liftedToInk(): the snapshot is the cross-fade the window forms out of, and a
    // lifted one flashed the wrong tone at the hand-off.
    auto* fx = new DisintegrateOverlay(host, snap);
    fx->ink = ink;
    fx->sweep = gather ? Sweep::SURFACE_IN : Sweep::SURFACE_OUT;
    fx->picture = picture;
    fx->target = QPointF(target);
    fx->sizeGridForDust(picture.size(), maxCells, SURFACE_CELL_PX);
    fx->placeForSurface(host, picture, target, escapeHost, alwaysEscape);
    fx->show();
    fx->raise();
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms > 0 ? ms : (gather ? SURFACE_IN_MS : SURFACE_OUT_MS));
    return fx;
  }

  // The floating-tip family's grab-and-fly; `paintNow` paints the first frame synchronously
  // so a closing tip hands over in one beat. Callers keep their own pre-guards.
  DisintegrateOverlay* flyTipDust(QWidget* subject, QWidget* host, const QPoint& originGlobal,
                                  bool gather, int ms, bool escapeHost, bool paintNow,
                                  bool alwaysEscape) {
    if (!subject || !host || !host->isVisible()) return nullptr;
    const QRect target(subject->mapToGlobal(QPoint(0, 0)), subject->size());
    if (target.width() < 8 || target.height() < 8) return nullptr;
    const QPixmap shot = subject->grab();
    if (shot.isNull()) return nullptr;
    const QRect box(host->mapFromGlobal(target.topLeft()), target.size());
    DisintegrateOverlay* fx = DisintegrateOverlay::overSurface(
        shot, box, host, host->mapFromGlobal(originGlobal), gather, ms,
        subject->palette().color(QPalette::WindowText),
        DisintegrateOverlay::SURFACE_MAX_CELLS, escapeHost, alwaysEscape);
    if (fx && paintNow) fx->repaint();
    return fx;
  }
}  // namespace stencil::gui
