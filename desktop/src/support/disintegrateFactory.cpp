#include "disintegrateOverlay.hpp"

namespace stencil::gui {

  // Snapshot `victim` and scatter it inside `host` (which must outlive the play —
  // normally the window). Returns nullptr when there is nothing worth animating, so
  // callers can treat a failure as "just remove it".
  // `cols`/`rows` override the grid for a denser scatter (the chat surfaces ask for
  // one — a deleted message is a deliberate act and deserves more than a list row's
  // dust; browser motion.js CHAT_DISINTEGRATE_*). 0 keeps the defaults.
  // `ms` shortens the flight for a caller on its own clock (a chat card arriving —
  // llm/chatWidgets.cpp kChatArriveMs); 0 keeps the row default.
  // `ink` lifts the grains towards the victim's own text colour (kSurfaceInkMix, the
  // browser's speckPainter): a list row and the list it leaves are the same colour,
  // and unlifted its dust was invisible over the rows behind it. A PICTURE (the canvas
  // image) passes none — its grains are its own pixels.
  DisintegrateOverlay* DisintegrateOverlay::over(QWidget* victim, QWidget* host, Sweep sweep,
                                                 int cols, int rows, int ms, const QColor& ink) {
    if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
    if (!victim || !host || !victim->isVisible()) return nullptr;
    if (victim->width() < 8 || victim->height() < 8) return nullptr;
    const QPixmap snap = victim->grab();
    if (snap.isNull()) return nullptr;
    // Placed in the host's coordinates: the victim is about to be destroyed, and a
    // child of it would die mid-flight.
    const QPoint at = victim->mapTo(host, QPoint(0, 0));
    auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
    fx->ink_ = ink;
    fx->sweep_ = sweep;
    if (sweep != Sweep::Rows) fx->sizeGridForDust(victim->size());
    if (cols > 0) fx->cols_ = cols;
    if (rows > 0) fx->rows_ = rows;
    fx->setGeometry(QRect(at, victim->size()));
    fx->show();
    fx->raise();
    // …and again once the pending layout has settled: removing the victim relayouts
    // its container, which restacks the host's children and would bury the particles.
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms > 0 ? ms : kMs);
    return fx;
  }


  // Same effect for a REGION of a widget — item views paint their rows, so a
  // deleted QListWidget/QTableWidget row has no widget of its own to grab.
  // `rect` is in `source` coordinates (a delegate's option.rect / visualItemRect).
  // `dust` sizes the grid from the rect's own pixels (kDustCellPx per mote) instead of the
  // fixed kCols x kRows — a wide, short LIST row split 22x11 came apart into a handful of
  // slabs rather than dust. Implied for the non-Rows sweeps, which always want motes.
  // `dustCells` caps the mote count for THIS overlay. Removing several rows at once means
  // several overlays repainting every frame, and the cost is their SUM — so a mass removal
  // divides the budget between them (browser motion.js scatterGridFor does the same).
  // `ms` shortens the flight for a motion that is not a removal — a filter change moves
  // rows in and out on its own short clock (support/filterFade.hpp kFilterDustMs).
  // `ink`: as over() — the row's text colour to lift its grains towards; none for a picture.
  // `shot` overrides the photograph: a caller that must HIDE the source before its motes
  // fly (a delegate-painted row waiting behind its own dust — filterFade's dustRowIn)
  // has to take the picture while the row is still inked, or the cloud is made of the
  // bare background it left behind and nothing is seen to arrive.
  DisintegrateOverlay* DisintegrateOverlay::overRect(QWidget* source, const QRect& rect,
                                                     QWidget* host, Sweep sweep, bool dust,
                                                     int dustCells, int ms, const QColor& ink,
                                                     const QPixmap& shot) {
    if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
    if (!source || !host || !source->isVisible()) return nullptr;
    if (rect.width() < 8 || rect.height() < 8) return nullptr;
    const QPixmap snap = shot.isNull() ? source->grab(rect) : shot;
    if (snap.isNull()) return nullptr;
    const QPoint at = source->mapTo(host, rect.topLeft());
    auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
    fx->ink_ = ink;
    fx->sweep_ = sweep;
    if (dust || sweep != Sweep::Rows) fx->sizeGridForDust(rect.size(), dustCells);
    fx->setGeometry(QRect(at, rect.size()));
    fx->show();
    fx->raise();
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms > 0 ? ms : kMs);
    return fx;
  }


  // The same scatter over an explicitly RENDERED pair of states, for a control whose
  // glyph belongs to the style rather than to a widget of its own (a checkbox
  // indicator): `particles` is the state that comes and goes, `base` the one left
  // behind, painted opaque underneath so whatever the real control already shows
  // never bleeds through. `at` is in host coordinates; `spread` scales the throw for
  // a control far smaller than a list row, and `ms` shortens it to click feedback.
  // `pad` widens the overlay around `at` WITHOUT moving the picture inside it: a row's
  // motes travel a fraction of its own width and clip harmlessly, but a 16px control's
  // leave its box at once, so the canvas has to be bigger than the thing on it.
  DisintegrateOverlay* DisintegrateOverlay::overPixmaps(const QPixmap& particles,
                                                        const QPixmap& base, const QRect& at,
                                                        QWidget* host, Sweep sweep, int cols,
                                                        int rows, int ms, double spread,
                                                        int pad, const QString& name) {
    if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
    if (!host || particles.isNull() || at.width() < 2 || at.height() < 2) return nullptr;
    auto* fx = new DisintegrateOverlay(host, particles);
    if (!name.isEmpty()) fx->setObjectName(name);
    fx->base_ = base;
    fx->sweep_ = sweep;
    fx->cols_ = std::max(1, cols);
    fx->rows_ = std::max(1, rows);
    fx->spread_ = spread;
    fx->pad_ = std::max(0, pad);
    fx->setGeometry(at.adjusted(-fx->pad_, -fx->pad_, fx->pad_, fx->pad_));
    fx->show();
    fx->raise();
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms);
    return fx;
  }


  // Everything a flight can paint — picture plus target point, each padded. All GLOBAL.
  // A layer smaller than this crops the cloud; placeForSurface() tests the host on it.
  QRect DisintegrateOverlay::surfaceLayerRect(const QRect& pictureGlobal,
                                              const QPoint& targetGlobal) {
    QRect need = pictureGlobal;
    need |= QRect(targetGlobal, QSize(1, 1));
    return need.adjusted(-kSurfacePadPx, -kSurfacePadPx, kSurfacePadPx, kSurfacePadPx);
  }


  // A whole surface's flight. `picture` is where the snapshot sits and `target` the
  // point its motes stream out of (SurfaceIn) or pour into (SurfaceOut), both in HOST
  // coordinates; the overlay covers the whole host rather than just the picture,
  // because a mote's journey is out to that point and a surface-sized layer clips it.
  // `escapeHost` is for a surface that is its own top-level window (a dialog, a menu,
  // the tooltip), whose cloud must be free to leave the host too — see
  // placeForSurface(). Ones living INSIDE it leave it false: the docked chat and the
  // toasts aim past the window edge on purpose and want to be cropped there.
  // `alwaysEscape`: a surface that is its own window floating ABOVE the host (the
  // projects hover preview) hides a child layer's motes underneath itself — this
  // lifts the cloud into the top-level layer even when the flight would fit the
  // host, so it rides above both (browser stacking, where the dust is z-topmost).
  DisintegrateOverlay* DisintegrateOverlay::overSurface(const QPixmap& snap,
                                                        const QRect& picture, QWidget* host,
                                                        const QPoint& target, bool gather,
                                                        int ms, const QColor& ink, int maxCells,
                                                        bool escapeHost, bool alwaysEscape) {
    if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
    if (!host || snap.isNull() || picture.width() < 8 || picture.height() < 8) return nullptr;
    // The TRUE window pixels, NOT liftedToInk(): the motes are the accent now, but the
    // snapshot is still the cross-fade the window forms out of, and lifting it toward the
    // ink flashed the wrong tone at the hand-off. The lift survives on the
    // non-surface flights (over/overRect), whose at-home cells still read it.
    auto* fx = new DisintegrateOverlay(host, snap);
    fx->ink_ = ink;
    fx->sweep_ = gather ? Sweep::SurfaceIn : Sweep::SurfaceOut;
    fx->picture_ = picture;
    fx->target_ = QPointF(target);
    fx->sizeGridForDust(picture.size(), maxCells, kSurfaceCellPx);
    fx->placeForSurface(host, picture, target, escapeHost, alwaysEscape);
    fx->show();
    fx->raise();
    QTimer::singleShot(0, fx, [fx] { fx->raise(); });
    fx->start(ms > 0 ? ms : (gather ? kSurfaceInMs : kSurfaceOutMs));
    return fx;
  }

  // Grab-and-fly for the floating-tip family (tooltips, the export preview, popup
  // menus, the projects hover preview): photograph `subject` and fly it as surface
  // dust inside `host`, out of / into `originGlobal`. Handles the size gate, host
  // mapping and ink lift; `paintNow` paints the first frame synchronously (a closing
  // tip hands over in one beat); `alwaysEscape` lifts the cloud into the top-level
  // layer even when it would fit the host (a tip window floating ABOVE the host would
  // hide a child layer's motes — see overSurface). Callers keep their own pre-guards
  // (motion, owner visibility, subject state).
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
        DisintegrateOverlay::kSurfaceMaxCells, escapeHost, alwaysEscape);
    if (fx && paintNow) fx->repaint();
    return fx;
  }
}  // namespace stencil::gui
