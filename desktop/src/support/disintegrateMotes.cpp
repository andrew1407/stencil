#include "disintegrateOverlay.hpp"

namespace stencil::gui {

  // One grain of a ROW flight — the browser's tileMotion + tileScatter keyframes, op
  // for op: the row crumbles from its top edge downward, each grain falling and
  // fanning out (signed, so the cloud spreads both ways), through the bend at 38% of
  // its flight on the first leg's curve and home… out, on the flight's own.
  // Same contract as fallingMote: false = the cell is the picture right now.
  bool DisintegrateOverlay::rowMote(const QRectF& box, int cx, int cy, double cw, double ch,
                                    Mote* out) const {
    const double n = cellNoise(cx, cy);
    const double m = cellNoise(cx + 41, cy + 17);
    const double q = cellNoise(cx + 97, cy + 53);
    // 0 at the TOP row (goes first), 1 at the bottom (goes last). The sweep is a fifth
    // of the span (browser: "a mote still at its 0% pose past the row's own collapse
    // is a dot screen sitting where the row was"), plus the tile's own jitter.
    const double progress = rows_ > 1 ? double(cy) / (rows_ - 1) : 0.0;
    const double delay = progress * 0.2 + n * (60.0 / 900.0);
    // The flight is what is left of the span, floored (kMinTileMs) so a late grain
    // still flies rather than blinks.
    const double flight = std::max(double(kMinTileMs) / std::max(1, ms_), 1.0 - delay);
    const double t = (t_ - delay) / flight;
    if (t <= 0.0) return false;   // not yet left: the picture
    *out = Mote{};
    if (t >= 1.0) return true;    // gone
    const QColor cell = grainColour(cx, cy, n);
    if (cell.alphaF() <= 0.02) return true;   // nothing was painted here
    const double tx = (m - 0.5) * 66 * spread_;
    const double ty = (26 + progress * 30 + n * 44) * spread_;
    const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
    const double w = cellNoise(cx + 13, cy + 71);
    const QPointF far = home + QPointF(tx, ty);
    const QPointF bend = home + waypointOf(tx, ty, q);
    out->at = legAt(t, kRowSplit, rowLegEase(), rowEase(), home, bend, far) + turbulenceAt(t, tx, ty, w);
    // tileScatter's scale: 1 at home, its far size (0.3..0.6) out there, halfway at the bend.
    const double farScale = 0.3 + n * 0.3;
    out->radius = moteRadius(cw, ch, n)
        * legScalar(t, kRowSplit, rowLegEase(), rowEase(), 1.0, 1.0 - (1.0 - farScale) * 0.5, farScale);
    finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * scatterAlpha(t), glintAt(cx, cy), w,
                tintAt(cx, cy), t, t, tx, ty, false, far);
    return true;
  }


  // One grain of a FALL / GATHER flight (browser motion.js ghostOut / ghostIn —
  // dustParts + drawDust). Returns false while the cell is at home —
  // not yet left, or already landed — and is then simply the picture. True means the
  // cell is cut out of it, with `out` the grain to draw (a radius of 0 once it is gone).
  bool DisintegrateOverlay::fallingMote(const QRectF& box, int cx, int cy, double cw, double ch,
                                        Mote* out) const {
    const double n = cellNoise(cx, cy);
    // A second, decorrelated hash for the SIDEWAYS drift, a third for the bend. With
    // one hash driving everything, whole diagonals moved together and the thing tore
    // like a sheet instead of coming apart (browser motion.js tileMotion twin).
    const double m = cellNoise(cx + 41, cy + 17);
    const double q = cellNoise(cx + 97, cy + 53);
    // Fall starts at the top; Gather at the bottom — it is Fall rewound, so the cell
    // that leaves first is the last one home.
    const double progress = rows_ > 1
        ? (sweep_ == Sweep::Fall ? double(cy) / (rows_ - 1)
                                 : double(rows_ - 1 - cy) / (rows_ - 1))
        : 0.0;
    const double delay = progress * 0.45 + n * 0.08;
    const bool gather = sweep_ == Sweep::Gather;
    double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
    if (!gather && t <= 0.0) return false;   // not yet left: the picture
    if (gather && t >= 1.0) return false;    // landed: the picture
    t = std::clamp(t, 0.0, 1.0);
    // How far from home the cell is: 1 = out there, 0 = in place. Scattering runs
    // 0→1; gathering is the same journey read backwards, eased so a mote covers most
    // of the distance early and settles (browser dustEase).
    const double away = gather ? std::pow(1.0 - t, 3.0) : t;
    *out = Mote{};
    if (away >= 1.0) return true;   // gone, or not yet set off: cut, nothing to draw
    const QColor cell = grainColour(cx, cy, n);
    if (cell.alphaF() <= 0.02) return true;   // nothing was painted here
    // A falling image drops (and accelerates, hence away²); a gathering one comes
    // FROM below and rises home — the fall inverted. The sideways fan is signed, so
    // the cloud spreads both ways.
    const double drift = (22 + progress * 34 + n * 30) * spread_;
    const double tx = (m - 0.5) * 66 * spread_;
    const double ty = drift * 1.6;
    const double fall = sweep_ == Sweep::Fall ? away * away : away;
    const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
    out->at = home + QPointF(away * tx, fall * ty) + swirlAt(away, tx, ty, q);
    out->radius = moteRadius(cw, ch, n) * (1.0 - away * (0.65 - n * 0.3));
    // No twinkle on a falling picture (browser drawDust has none); a styled one still
    // breathes, on the same fourth hash every cloud keys its style off.
    finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * (gather ? gatherAlpha(t) : scatterAlpha(t)),
                false, cellNoise(cx + 13, cy + 71), tintAt(cx, cy), t, away, tx, ty, gather,
                gather ? home : home + QPointF(tx, ty));
    return true;
  }


  // One grain of a SURFACE flight — the Qt twin of browser motion.js surfaceMotion plus
  // the tileGatherSurface / tileScatterSurface keyframes. The path is the cell's own
  // offset to the target, so every mote converges there instead of falling; the two
  // decorrelated hashes only fan the arrival, the third bends it. The delay rides the
  // DISTANCE, so the edge nearest the point goes first and the far one last. Halved
  // for a SCATTER (browser motion.js surfaceMotion delayScale): held at its 0% pose
  // for up to 45% of the flight, a mote is indistinguishable from the surface not
  // having reacted yet — on a big surface (a full-height docked chat panel) that read
  // as nothing moving at all until a sudden, late flick, not sand leaving.
  // Same contract as fallingMote: false = the cell is the picture right now.
  bool DisintegrateOverlay::surfaceMote(const QRectF& box, int cx, int cy, double cw, double ch,
                                        Mote* out) const {
    const double n = cellNoise(cx, cy);
    const double m = cellNoise(cx + 41, cy + 17);
    const double q = cellNoise(cx + 97, cy + 53);
    const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
    const QPointF tgt = target_ + QPointF(shift_);
    const double toX = tgt.x() - home.x();
    const double toY = tgt.y() - home.y();
    // Normalised against the longest trip any cell in this box makes, so the sweep
    // fills the whole flight whatever the point's distance is.
    const double reach = std::hypot(toX, toY);
    const double far = std::hypot(box.width(), box.height()) + reach;
    const double progress = far > 0 ? std::min(1.0, reach / far) : 0.0;
    const bool gather = sweep_ == Sweep::SurfaceIn;
    const double delay = (progress * 0.45 + n * 0.12) * (gather ? 1.0 : 0.5);
    double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
    if (!gather && t <= 0.0) return false;   // still the surface
    *out = Mote{};
    // A gathering grain is NOTHING until it sets off: parked at the point with hundreds
    // of others it filled the icon with a solid blob of the accent.
    if (gather && t <= 0.0) return true;     // cut from the picture, nothing drawn yet
    t = std::clamp(t, 0.0, 1.0);
    if (!gather && t >= 1.0) return true;    // a scattered mote that has finished is gone
    const QColor cell = grainColour(cx, cy, n);
    if (cell.alphaF() <= 0.02) return true;
    // The whole cloud's own alpha (browser dustHostOut / dustHostIn): a forming cloud
    // stays whole until 58% of the span, then fades out under the surface fading up —
    // a LANDED mote sits at home at full size until then, a grain of the dot screen
    // the window is cross-fading out of, never a hole. A leaving cloud fades in over
    // the first beat, as the surface under it cuts out.
    // The cloud clears before the window is substantially opaque (kSurfaceMoteFade*):
    // IN it holds, then fades out over the first part of the window's fade-up; OUT it
    // waits until the leaving window has begun to cut out, then rises.
    const double host = gather
        ? (t_ < kDustHold ? 1.0
           : std::max(0.0, 1.0 - (t_ - kDustHold) / ((1.0 - kDustHold) * kSurfaceMoteFadeFrac)))
        : std::clamp((t_ - kSurfaceScatterSplit * kSurfaceMoteRiseDelay) / kSurfaceScatterSplit,
                     0.0, 1.0);
    if (gather && t >= 1.0) {
      out->at = home;
      out->radius = moteRadius(cw, ch, n);
      finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * host, glintAt(cx, cy),
                  cellNoise(cx + 13, cy + 71), tintAt(cx, cy), 1.0, 0.0, toX, toY, true, home);
      return true;
    }
    // A gathering mote waits at the point until its delay is up, which is what makes
    // the stream read as pouring out. The ramps ride the clock (browser dustCloud.js
    // FLIGHTS surfaceGather / surfaceScatter), not the eased distance — see scatterAlpha.
    // A SCATTER fades to nothing by 82%, not at the very end: every mote converges on the
    // one icon point, so a tail still at ~0.2 opacity piled into a solid accent blob that
    // blinked out. Matches surfaceScatter's alpha stops.
    const double alpha = host * (gather ? (t < 0.45 ? 0.55 + 0.45 * (t / 0.45) : 1.0)
                                        : (t < 0.5 ? 1.0 - t * 0.3
                                                   : std::max(0.0, 0.85 * (1.0 - (t - 0.5) / 0.32))));
    const double tx = toX + (m - 0.5) * kSurfaceSpreadPx;
    const double ty = toY + (n - 0.5) * kSurfaceSpreadPx;
    // Two legs (the browser's mid keyframe): a gather flies far → bend over its first
    // 16% on the leg curve, then bend → home on the ease-out; a scatter home → bend
    // over 18%, then out. Most of the distance goes early either way — the bend sits
    // where it can be seen — and the turn is a flick, which is what sand does.
    const double w = cellNoise(cx + 13, cy + 71);
    const QPointF point = home + QPointF(tx, ty);
    const QPointF bend = home + waypointOf(tx, ty, q);
    const QPointF wobble = turbulenceAt(t, tx, ty, w);
    const double farScale = 0.12 + n * 0.25;
    const double midScale = 1.0 - (1.0 - farScale) * 0.5;
    if (gather) {
      out->at = legAt(t, kSurfaceGatherSplit, surfaceLegEase(), surfaceEase(), point, bend, home) + wobble;
      out->radius = moteRadius(cw, ch, n)
          * legScalar(t, kSurfaceGatherSplit, surfaceLegEase(), surfaceEase(), farScale, midScale, 1.0);
    } else {
      out->at = legAt(t, kSurfaceScatterSplit, surfaceLegEase(), surfaceEase(), home, bend, point) + wobble;
      out->radius = moteRadius(cw, ch, n)
          * legScalar(t, kSurfaceScatterSplit, surfaceLegEase(), surfaceEase(), 1.0, midScale, farScale);
    }
    finishGrain(out, cell.alphaF() * alpha * (0.78 + n * 0.22), glintAt(cx, cy), w,
                tintAt(cx, cy), t, gather ? 1.0 - t : t, tx, ty, gather, gather ? home : point);
    return true;
  }
}  // namespace stencil::gui
