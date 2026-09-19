#include "DisintegrateOverlay.hpp"

namespace stencil::gui {

  // A ROW grain — browser tileMotion + tileScatter, op for op. false = still the picture.
  bool DisintegrateOverlay::rowMote(const QRectF& box, int cx, int cy, double cw, double ch,
                                    Mote* out) const {
    const double n = cellNoise(cx, cy);
    const double m = cellNoise(cx + 41, cy + 17);
    const double q = cellNoise(cx + 97, cy + 53);
    // Top row first. The sweep is a fifth of the span plus the tile's jitter (browser twin).
    const double progress = rows_ > 1 ? double(cy) / (rows_ - 1) : 0.0;
    const double delay = progress * 0.2 + n * (60.0 / 900.0);
    const double flight = std::max(double(MIN_TILE_MS) / std::max(1, ms_), 1.0 - delay);
    const double t = (t_ - delay) / flight;
    if (t <= 0.0) return false;
    *out = Mote{};
    if (t >= 1.0) return true;
    const QColor cell = grainColour(cx, cy, n);
    if (cell.alphaF() <= 0.02) return true;
    const double tx = (m - 0.5) * 66 * spread_;
    const double ty = (26 + progress * 30 + n * 44) * spread_;
    const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
    const double w = cellNoise(cx + 13, cy + 71);
    const QPointF far = home + QPointF(tx, ty);
    const QPointF bend = home + waypointOf(tx, ty, q);
    out->at = legAt(t, ROW_SPLIT, rowLegEase(), rowEase(), home, bend, far) + turbulenceAt(t, tx, ty, w);
    // tileScatter's scale: 1 at home, 0.3..0.6 out there, halfway at the bend.
    const double farScale = 0.3 + n * 0.3;
    out->radius = moteRadius(cw, ch, n)
        * legScalar(t, ROW_SPLIT, rowLegEase(), rowEase(), 1.0, 1.0 - (1.0 - farScale) * 0.5, farScale);
    finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * scatterAlpha(t), glintAt(cx, cy), w,
                tintAt(cx, cy), t, t, tx, ty, false, far);
    return true;
  }


  // A FALL / GATHER grain (browser motion.js ghostOut / ghostIn). false = at home, the
  // picture; true = cut out, `out` is the grain (radius 0 once gone).
  bool DisintegrateOverlay::fallingMote(const QRectF& box, int cx, int cy, double cw, double ch,
                                        Mote* out) const {
    const double n = cellNoise(cx, cy);
    // Decorrelated hashes for drift and bend: with one, whole diagonals tore like a sheet.
    const double m = cellNoise(cx + 41, cy + 17);
    const double q = cellNoise(cx + 97, cy + 53);
    // Gather is Fall rewound: the cell that leaves first is the last one home.
    const double progress = rows_ > 1
        ? (sweep_ == Sweep::FALL ? double(cy) / (rows_ - 1)
                                 : double(rows_ - 1 - cy) / (rows_ - 1))
        : 0.0;
    const double delay = progress * 0.45 + n * 0.08;
    const bool gather = sweep_ == Sweep::GATHER;
    double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
    if (!gather && t <= 0.0) return false;
    if (gather && t >= 1.0) return false;
    t = std::clamp(t, 0.0, 1.0);
    // 1 = out there, 0 = home; a gather is the journey read backwards (browser dustEase).
    const double away = gather ? std::pow(1.0 - t, 3.0) : t;
    *out = Mote{};
    if (away >= 1.0) return true;
    const QColor cell = grainColour(cx, cy, n);
    if (cell.alphaF() <= 0.02) return true;
    // A fall accelerates (away²); a gather rises home from below.
    const double drift = (22 + progress * 34 + n * 30) * spread_;
    const double tx = (m - 0.5) * 66 * spread_;
    const double ty = drift * 1.6;
    const double fall = sweep_ == Sweep::FALL ? away * away : away;
    const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
    out->at = home + QPointF(away * tx, fall * ty) + swirlAt(away, tx, ty, q);
    out->radius = moteRadius(cw, ch, n) * (1.0 - away * (0.65 - n * 0.3));
    // No twinkle on a falling picture (browser drawDust has none).
    finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * (gather ? gatherAlpha(t) : scatterAlpha(t)),
                false, cellNoise(cx + 13, cy + 71), tintAt(cx, cy), t, away, tx, ty, gather,
                gather ? home : home + QPointF(tx, ty));
    return true;
  }


  // A SURFACE grain - browser motion.js surfaceMotion + tileGatherSurface/tileScatterSurface. The
  // delay rides the DISTANCE to the target, halved for a scatter. false = the picture.
  bool DisintegrateOverlay::surfaceMote(const QRectF& box, int cx, int cy, double cw, double ch,
                                        Mote* out) const {
    const double n = cellNoise(cx, cy);
    const double m = cellNoise(cx + 41, cy + 17);
    const double q = cellNoise(cx + 97, cy + 53);
    const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
    const QPointF tgt = target_ + QPointF(shift_);
    const double toX = tgt.x() - home.x();
    const double toY = tgt.y() - home.y();
    // Normalised against the longest trip any cell in this box makes.
    const double reach = std::hypot(toX, toY);
    const double far = std::hypot(box.width(), box.height()) + reach;
    const double progress = far > 0 ? std::min(1.0, reach / far) : 0.0;
    const bool gather = sweep_ == Sweep::SURFACE_IN;
    const double delay = (progress * 0.45 + n * 0.12) * (gather ? 1.0 : 0.5);
    double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
    if (!gather && t <= 0.0) return false;
    *out = Mote{};
    // A gathering grain is NOTHING until it sets off: parked, hundreds make a solid blob.
    if (gather && t <= 0.0) return true;
    t = std::clamp(t, 0.0, 1.0);
    if (!gather && t >= 1.0) return true;
    const QColor cell = grainColour(cx, cy, n);
    if (cell.alphaF() <= 0.02) return true;
    // The whole cloud's alpha (browser dustHostOut / dustHostIn): IN holds, then fades
    // under the window's fade-up; OUT waits for the window to begin cutting out.
    const double host = gather
        ? (t_ < DUST_HOLD ? 1.0
           : std::max(0.0, 1.0 - (t_ - DUST_HOLD) / ((1.0 - DUST_HOLD) * SURFACE_MOTE_FADE_FRAC)))
        : std::clamp((t_ - SURFACE_SCATTER_SPLIT * SURFACE_MOTE_RISE_DELAY) / SURFACE_SCATTER_SPLIT,
                     0.0, 1.0);
    if (gather && t >= 1.0) {
      out->at = home;
      out->radius = moteRadius(cw, ch, n);
      finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * host, glintAt(cx, cy),
                  cellNoise(cx + 13, cy + 71), tintAt(cx, cy), 1.0, 0.0, toX, toY, true, home);
      return true;
    }
    // Ramps ride the clock (browser dustCloud.js FLIGHTS surfaceGather / surfaceScatter).
    // A scatter is gone by 82%: a tail converging on one point piled into a blob.
    const double alpha = host * (gather ? (t < 0.45 ? 0.55 + 0.45 * (t / 0.45) : 1.0)
                                        : (t < 0.5 ? 1.0 - t * 0.3
                                                   : std::max(0.0, 0.85 * (1.0 - (t - 0.5) / 0.32))));
    const double tx = toX + (m - 0.5) * SURFACE_SPREAD_PX;
    const double ty = toY + (n - 0.5) * SURFACE_SPREAD_PX;
    // Two legs (the browser's mid keyframe): most of the distance goes early.
    const double w = cellNoise(cx + 13, cy + 71);
    const QPointF point = home + QPointF(tx, ty);
    const QPointF bend = home + waypointOf(tx, ty, q);
    const QPointF wobble = turbulenceAt(t, tx, ty, w);
    const double farScale = 0.12 + n * 0.25;
    const double midScale = 1.0 - (1.0 - farScale) * 0.5;
    if (gather) {
      out->at = legAt(t, SURFACE_GATHER_SPLIT, surfaceLegEase(), surfaceEase(), point, bend, home) + wobble;
      out->radius = moteRadius(cw, ch, n)
          * legScalar(t, SURFACE_GATHER_SPLIT, surfaceLegEase(), surfaceEase(), farScale, midScale, 1.0);
    } else {
      out->at = legAt(t, SURFACE_SCATTER_SPLIT, surfaceLegEase(), surfaceEase(), home, bend, point) + wobble;
      out->radius = moteRadius(cw, ch, n)
          * legScalar(t, SURFACE_SCATTER_SPLIT, surfaceLegEase(), surfaceEase(), 1.0, midScale, farScale);
    }
    finishGrain(out, cell.alphaF() * alpha * (0.78 + n * 0.22), glintAt(cx, cy), w,
                tintAt(cx, cy), t, gather ? 1.0 - t : t, tx, ty, gather, gather ? home : point);
    return true;
  }
}  // namespace stencil::gui
