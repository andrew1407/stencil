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
#include <QEasingCurve>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

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

    // Which way the sweep runs. A ROW erodes upward off a list (Rows = bottom→top);
    // an IMAGE falls apart from its top edge and the pieces drop (Fall = top→bottom);
    // GATHER is Fall played backwards — the motes start below where they belong and rise
    // into place, fading up, so an arriving image assembles bottom→top exactly as the
    // clear erodes it top-down (browser parity: ghostIn vs ghostOut in js/ui/motion.js).
    enum class Sweep { Rows, Fall, Gather };

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

   protected:
    void paintEvent(QPaintEvent*) override {
      if (snap_.isNull()) return;
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      const double cw = double(width()) / cols_;
      const double ch = double(height()) / rows_;
      const double sx = double(snap_.width()) / width();     // snapshot is DPR-scaled
      const double sy = double(snap_.height()) / height();
      for (int cy = 0; cy < rows_; ++cy) {
        for (int cx = 0; cx < cols_; ++cx) {
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
          const QRectF dst(cx * cw, cy * ch, cw, ch);
          const QRectF src(dst.x() * sx, dst.y() * sy, dst.width() * sx, dst.height() * sy);
          p.save();
          p.setOpacity(1.0 - away);
          // Fan out sideways rather than all sliding one way.
          p.translate(dst.center());
          // Rows rise off a list; a falling image drops (and accelerates, hence away²);
          // a gathering one comes FROM below and rises home — the fall inverted.
          const double drift = 22 + progress * 34 + n * 30;
          const double dy = sweep_ == Sweep::Fall ? away * away * drift * 1.6
                          : gather               ? away * drift * 1.6
                                                 : -away * drift;
          p.translate(away * ((m - 0.5) * 66), dy);
          p.rotate(away * (m - 0.5) * 70);
          const double scale = 1.0 - away * (0.65 - n * 0.3);
          p.scale(scale, scale);
          p.translate(-dst.center());
          p.drawPixmap(dst, snap_, src);
          p.restore();
        }
      }
    }

   private:
    DisintegrateOverlay(QWidget* host, const QPixmap& snap) : QWidget(host), snap_(snap) {
      setObjectName(kObjectName);   // findable without a Q_OBJECT (this class stays MOC-free)
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
    }

    void start() {
      auto* anim = new QVariantAnimation(this);
      anim->setDuration(kMs);
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
    void sizeGridForDust(const QSize& size, int maxCells = kDustMaxCells) {
      cols_ = std::max(1, qRound(double(size.width()) / kDustCellPx));
      rows_ = std::max(1, qRound(double(size.height()) / kDustCellPx));
      while (cols_ * rows_ > std::max(64, maxCells)) {
        // ceil(x/1.1) is x itself for x ≤ 10 — force a strict shrink or this spins forever
        // (a mass removal's shared budget gets small enough to reach that range).
        cols_ = std::max(1, std::min(cols_ - 1, int(std::ceil(cols_ / 1.1))));
        rows_ = std::max(1, std::min(rows_ - 1, int(std::ceil(rows_ / 1.1))));
      }
    }

    QPixmap snap_;
    Sweep sweep_ = Sweep::Rows;
    int cols_ = kCols;
    int rows_ = kRows;
    double t_ = 0.0;
  };

}  // namespace stencil::gui
