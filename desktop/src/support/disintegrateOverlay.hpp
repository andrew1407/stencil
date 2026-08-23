#pragma once
// Disintegration ("the snap") — the desktop port of disintegrate() in
// browser/js/ui/motion.js.
//
// The browser clones the element once per grid cell and lets CSS scatter the clones.
// Qt has no cloning to do: the widget is grabbed ONCE and the snapshot is redrawn
// cell by cell, each cell offset/rotated/faded by its own progress. One animation
// drives the lot, so it stays a single repaint per frame however many cells there are.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include <QColor>
#include <QEasingCurve>
#include <QPainter>
#include <QPointF>
#include <QPaintEvent>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace stencil::gui {

  class DisintegrateOverlay : public QWidget {
   public:
    static constexpr int kMs = 900;      // browser DISINTEGRATE_MS
    // A fine grid: at 8x4 the cells read as big rectangles sliding apart, not as ash.
    // Cheap here — the snapshot is redrawn per cell, nothing is cloned.
    static constexpr int kCols = 22;     // browser DISINTEGRATE_COLS
    static constexpr int kRows = 11;     // browser DISINTEGRATE_ROWS
    // A falling IMAGE is sized on screen instead: a fixed grid over a big canvas gives
    // big rectangles, not dust (browser DUST_CELL_PX). Cells are capped in count so a
    // huge canvas can't cost more per frame than the effect is worth.
    static constexpr int kDustCellPx = 7;   // browser motion.js MOTE_PX — keep the two in step
    static constexpr int kDustMaxCells = 7000;
    static constexpr const char* kObjectName = "stencilDisintegrate";
    // ── A whole SURFACE is dust too (browser motion.js surfaceIn / surfaceOut) ──
    // A dialog, a popup menu and the tooltip form from motes streaming out of the control
    // that opened them and come apart into motes pouring back in. Same snapshot, same
    // hashes; only the flight differs — every mote aims at ONE point instead of falling.
    static constexpr int kSurfaceInMs = 620;    // browser SURFACE_IN_MS
    static constexpr int kSurfaceOutMs = 380;   // browser SURFACE_OUT_MS
    static constexpr int kSurfaceCellPx = 6;    // browser SURFACE_MOTE_PX
    static constexpr int kSurfaceMaxCells = 3000;
    static constexpr double kSurfaceSpreadPx = 34;   // browser SURFACE_SPREAD
    // How far a surface's motes are lifted towards the window's own INK before they fly
    // (browser motion.js MOTE_INK). Without it the cloud is invisible: a window and the
    // one behind it are the same family of colour, so a dark dialog came apart into dark
    // motes over a dark page and the flight simply could not be seen. Mixing in the ink
    // keeps every mote the window's own colour and gives it something to read against,
    // and it flips with the theme for free — ink always contrasts with its background.
    static constexpr double kSurfaceInkMix = 0.42;

    // Which way the sweep runs. A ROW erodes upward off a list (Rows = bottom→top);
    // an IMAGE falls apart from its top edge and the pieces drop (Fall = top→bottom);
    // GATHER is Fall played backwards — the motes start below where they belong and rise
    // into place, fading up, so an arriving image assembles bottom→top exactly as the
    // clear erodes it top-down (browser parity: ghostIn vs ghostOut in js/ui/motion.js).
    enum class Sweep { Rows, Fall, Gather, SurfaceIn, SurfaceOut };

    // Deterministic per-cell jitter — the same hash the browser uses, so the two
    // scatter alike. Returns 0..1.
    static double cellNoise(int cx, int cy) {
      const double h = std::sin(cx * 127.1 + cy * 311.7) * 43758.5453;
      return h - std::floor(h);
    }

    // Snapshot `victim` and scatter it inside `host` (which must outlive the play —
    // normally the window). Returns nullptr when there is nothing worth animating, so
    // callers can treat a failure as "just remove it".
    // `cols`/`rows` override the grid for a denser scatter (the chat surfaces ask for
    // one — a deleted message is a deliberate act and deserves more than a list row's
    // dust; browser motion.js CHAT_DISINTEGRATE_*). 0 keeps the defaults.
    static DisintegrateOverlay* over(QWidget* victim, QWidget* host, Sweep sweep = Sweep::Rows,
                                     int cols = 0, int rows = 0) {
      if (!victim || !host || !victim->isVisible()) return nullptr;
      if (victim->width() < 8 || victim->height() < 8) return nullptr;
      const QPixmap snap = victim->grab();
      if (snap.isNull()) return nullptr;
      // Placed in the host's coordinates: the victim is about to be destroyed, and a
      // child of it would die mid-flight.
      const QPoint at = victim->mapTo(host, QPoint(0, 0));
      auto* fx = new DisintegrateOverlay(host, snap);
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
      fx->start();
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
    static DisintegrateOverlay* overRect(QWidget* source, const QRect& rect, QWidget* host,
                                        Sweep sweep = Sweep::Rows, bool dust = false,
                                        int dustCells = kDustMaxCells) {
      if (!source || !host || !source->isVisible()) return nullptr;
      if (rect.width() < 8 || rect.height() < 8) return nullptr;
      const QPixmap snap = source->grab(rect);
      if (snap.isNull()) return nullptr;
      const QPoint at = source->mapTo(host, rect.topLeft());
      auto* fx = new DisintegrateOverlay(host, snap);
      fx->sweep_ = sweep;
      if (dust || sweep != Sweep::Rows) fx->sizeGridForDust(rect.size(), dustCells);
      fx->setGeometry(QRect(at, rect.size()));
      fx->show();
      fx->raise();
      QTimer::singleShot(0, fx, [fx] { fx->raise(); });
      fx->start();
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
    static DisintegrateOverlay* overPixmaps(const QPixmap& particles, const QPixmap& base,
                                            const QRect& at, QWidget* host, Sweep sweep,
                                            int cols, int rows, int ms, double spread,
                                            int pad = 0, const QString& name = QString()) {
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

    // A whole surface's flight. `picture` is where the snapshot sits and `target` the
    // point its motes stream out of (SurfaceIn) or pour into (SurfaceOut), both in HOST
    // coordinates; the overlay itself covers the whole host, because a mote's whole
    // journey is out to that point and a layer the size of the surface would clip it.
    static DisintegrateOverlay* overSurface(const QPixmap& snap, const QRect& picture,
                                            QWidget* host, const QPoint& target, bool gather,
                                            int ms = 0, const QColor& ink = QColor()) {
      if (!host || snap.isNull() || picture.width() < 8 || picture.height() < 8) return nullptr;
      auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
      fx->sweep_ = gather ? Sweep::SurfaceIn : Sweep::SurfaceOut;
      fx->picture_ = picture;
      fx->target_ = QPointF(target);
      fx->sizeGridForDust(picture.size(), kSurfaceMaxCells, kSurfaceCellPx);
      fx->setGeometry(host->rect());
      fx->show();
      fx->raise();
      QTimer::singleShot(0, fx, [fx] { fx->raise(); });
      fx->start(ms > 0 ? ms : (gather ? kSurfaceInMs : kSurfaceOutMs));
      return fx;
    }

    // What a SURFACE flight is aimed at (the centre of the control it belongs to, in
    // HOST coordinates), where its snapshot sits, and which way it is going. The GUI
    // test reads these to prove a window really does come out of the icon that opened
    // it — the property the old ghost's start geometry used to carry.
    QPoint surfaceTarget() const { return target_.toPoint(); }
    QRect surfacePicture() const { return picture_; }
    bool gathering() const { return sweep_ == Sweep::SurfaceIn; }
    const QPixmap& snapshot() const { return snap_; }

   protected:
    void paintEvent(QPaintEvent*) override {
      if (snap_.isNull()) return;
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      // Where the picture itself sits — the whole overlay unless `pad` widened it, or a
      // SURFACE placed it somewhere inside a host-sized layer.
      const QRectF box = picture_.isValid() ? QRectF(picture_)
                                            : QRectF(rect()).adjusted(pad_, pad_, -pad_, -pad_);
      if (box.width() <= 0 || box.height() <= 0) return;
      // The state left behind, under the particles.
      if (!base_.isNull()) p.drawPixmap(box, base_, QRectF(base_.rect()));
      const double cw = box.width() / cols_;
      const double ch = box.height() / rows_;
      const double sx = double(snap_.width()) / box.width();   // snapshot is DPR-scaled
      const double sy = double(snap_.height()) / box.height();
      const bool surface = sweep_ == Sweep::SurfaceIn || sweep_ == Sweep::SurfaceOut;
      for (int cy = 0; cy < rows_; ++cy) {
        for (int cx = 0; cx < cols_; ++cx) {
          if (surface) { paintSurfaceCell(p, box, cx, cy, cw, ch, sx, sy); continue; }
          // The sweep runs BOTTOM→TOP: a cell's clock starts later the higher it sits,
          // so the silhouette erodes upward and the top is the last thing standing.
          const double n = cellNoise(cx, cy);
          // A second, decorrelated hash for the SIDEWAYS drift and the spin. With one
          // hash driving all three, whole diagonals moved together and the thing tore
          // like a sheet instead of coming apart (browser motion.js tileMotion twin).
          const double m = cellNoise(cx + 41, cy + 17);
          // Fall starts at the top; Rows and Gather start at the bottom — Gather because
          // it is Fall rewound, so the cell that leaves first is the last one home.
          const double progress = rows_ > 1
              ? (sweep_ == Sweep::Fall ? double(cy) / (rows_ - 1)
                                       : double(rows_ - 1 - cy) / (rows_ - 1))
              : 0.0;
          const double delay = progress * 0.45 + n * 0.08;
          double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
          if (t <= 0.0) t = 0.0;
          const bool gather = sweep_ == Sweep::Gather;
          if (!gather && t >= 1.0) continue;         // this cell is already gone
          if (gather && t <= 0.0) continue;          // …this one has not set off yet
          if (t > 1.0) t = 1.0;
          // How far from home the cell is: 1 = out there, 0 = in place. Scattering runs
          // 0→1; gathering is the same journey read backwards, eased so a mote covers
          // most of the distance early and settles (browser dustEase).
          const double away = gather ? std::pow(1.0 - t, 3.0) : t;
          const QRectF dst(box.x() + cx * cw, box.y() + cy * ch, cw, ch);
          const QRectF src(cx * cw * sx, cy * ch * sy, cw * sx, ch * sy);
          p.save();
          p.setOpacity(1.0 - away);
          // Fan out sideways rather than all sliding one way.
          p.translate(dst.center());
          // Rows rise off a list; a falling image drops (and accelerates, hence away²);
          // a gathering one comes FROM below and rises home — the fall inverted.
          const double drift = (22 + progress * 34 + n * 30) * spread_;
          const double dy = sweep_ == Sweep::Fall ? away * away * drift * 1.6
                          : gather               ? away * drift * 1.6
                                                 : -away * drift;
          p.translate(away * ((m - 0.5) * 66 * spread_), dy);
          p.rotate(away * (m - 0.5) * 70);
          const double scale = 1.0 - away * (0.65 - n * 0.3);
          p.scale(scale, scale);
          p.translate(-dst.center());
          p.drawPixmap(dst, snap_, src);
          p.restore();
        }
      }
    }

    // One mote of a SURFACE flight — the Qt twin of browser motion.js surfaceMotion plus
    // the tileGatherSurface / tileScatterSurface keyframes. The path is the cell's own
    // offset to the target, so every mote converges there instead of falling; the two
    // decorrelated hashes only fan the arrival. The delay rides the DISTANCE, so the edge
    // nearest the point goes first and the far one last.
    void paintSurfaceCell(QPainter& p, const QRectF& box, int cx, int cy,
                          double cw, double ch, double sx, double sy) {
      const double n = cellNoise(cx, cy);
      const double m = cellNoise(cx + 41, cy + 17);
      const QRectF dst(box.x() + cx * cw, box.y() + cy * ch, cw, ch);
      const QPointF home = dst.center();
      const double toX = target_.x() - home.x();
      const double toY = target_.y() - home.y();
      // Normalised against the longest trip any cell in this box makes, so the sweep
      // fills the whole flight whatever the point's distance is.
      const double reach = std::hypot(toX, toY);
      const double far = std::hypot(box.width(), box.height()) + reach;
      const double progress = far > 0 ? std::min(1.0, reach / far) : 0.0;
      const double delay = progress * 0.45 + n * 0.12;
      double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
      t = std::clamp(t, 0.0, 1.0);
      const bool gather = sweep_ == Sweep::SurfaceIn;
      // Ease-out both ways: the motes break away (or arrive) at once and drift to a stop,
      // which is what sand does. `away` is 1 out at the point, 0 home. The curve is a
      // file-static: building one per cell per frame is thousands of allocations a frame.
      static const QEasingCurve kOut(QEasingCurve::OutQuint);
      const double e = kOut.valueForProgress(t);
      const double away = gather ? 1.0 - e : e;
      // A scattered mote that has finished is simply gone; a gathering one waits at the
      // point until its delay is up, which is what makes the stream read as pouring out.
      double alpha;
      if (gather) alpha = e < 0.45 ? 0.55 + 0.45 * (e / 0.45) : 1.0;
      else if (e >= 1.0) return;
      else alpha = e < 0.55 ? 1.0 - e * 0.18 : 0.9 * (1.0 - (e - 0.55) / 0.45);
      const QRectF src(cx * cw * sx, cy * ch * sy, cw * sx, ch * sy);
      p.save();
      p.setOpacity(std::clamp(alpha, 0.0, 1.0));
      p.translate(home);
      p.translate(away * (toX + (m - 0.5) * kSurfaceSpreadPx),
                  away * (toY + (n - 0.5) * kSurfaceSpreadPx));
      p.rotate(away * (m - 0.5) * 60);
      const double scale = 1.0 - away * (1.0 - (0.12 + n * 0.25));
      p.scale(scale, scale);
      p.translate(-home);
      p.drawPixmap(dst, snap_, src);
      p.restore();
    }

   private:
    // The snapshot, mixed towards `ink` — one pass over the picture, so every mote is
    // already lifted by the time it is drawn. An invalid ink leaves it exactly as taken.
    static QPixmap liftedToInk(const QPixmap& snap, const QColor& ink) {
      if (!ink.isValid() || snap.isNull()) return snap;
      QPixmap out = snap;
      QPainter p(&out);
      p.setCompositionMode(QPainter::CompositionMode_SourceAtop);   // tints, never spreads
      p.fillRect(out.rect(), QColor(ink.red(), ink.green(), ink.blue(),
                                    qRound(255 * kSurfaceInkMix)));
      return out;
    }

    DisintegrateOverlay(QWidget* host, const QPixmap& snap) : QWidget(host), snap_(snap) {
      setObjectName(kObjectName);   // findable without a Q_OBJECT (this class stays MOC-free)
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
    }

    void start(int ms = kMs) {
      auto* anim = new QVariantAnimation(this);
      anim->setDuration(std::max(1, ms));
      anim->setEasingCurve(QEasingCurve::Linear);   // the per-cell delays own the shaping
      anim->setStartValue(0.0);
      anim->setEndValue(1.0);
      connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        t_ = v.toDouble();
        update();
      });
      connect(anim, &QVariantAnimation::finished, this, [this] { deleteLater(); });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // Dust motes sized on screen rather than as a share of the image, thinned back
    // if that would exceed the per-frame ceiling.
    void sizeGridForDust(const QSize& size, int maxCells = kDustMaxCells,
                         int cellPx = kDustCellPx) {
      cols_ = std::max(1, qRound(double(size.width()) / cellPx));
      rows_ = std::max(1, qRound(double(size.height()) / cellPx));
      while (cols_ * rows_ > std::max(64, maxCells)) {
        // ceil(x/1.1) is x itself for x ≤ 10 — force a strict shrink or this spins forever
        // (a mass removal's shared budget gets small enough to reach that range).
        cols_ = std::max(1, std::min(cols_ - 1, int(std::ceil(cols_ / 1.1))));
        rows_ = std::max(1, std::min(rows_ - 1, int(std::ceil(rows_ / 1.1))));
      }
    }

    QPixmap snap_;
    QPixmap base_;          // the state left behind (overPixmaps only); null = nothing
    Sweep sweep_ = Sweep::Rows;
    int cols_ = kCols;
    int rows_ = kRows;
    int pad_ = 0;           // slack around the picture for the motes to fly into
    QRect picture_;         // where the snapshot sits (surfaces only); invalid = the whole box
    QPointF target_;        // the point a surface's motes stream out of / pour into
    double spread_ = 1.0;   // throw distance, as a share of a list row's
    double t_ = 0.0;
  };

}  // namespace stencil::gui
