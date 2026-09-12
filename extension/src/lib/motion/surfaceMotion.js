// ── Surfaces: a menu, a dialog and a mini popup are dust too ────────────────
// Browser motion.js twin. A modal and the ⋯/context/dropdown popups play the SAME scatter
// a deleted row does, only every mote flies into (or out of) the point that owns the
// surface. IN is the slower half on purpose — a surface forming is the thing you watch;
// going out is brisk, you have already decided.
import { tileNoise, tileWaypoint } from './tiles.js';
export const SURFACE_IN_MS = 620;
export const SURFACE_OUT_MS = 380;
// A MENU is opened to be clicked, often blind, so it may not spend half a second forming.
// Its own, brisker clock — the flight is the same one.
export const SURFACE_MENU_IN_MS = 340;
export const SURFACE_MENU_OUT_MS = 220;

// The grain a mote AIMS for, and the ceiling on how many a flight may cost. A window is
// tens of times a row's area, so the budget is what sizes its cells: at 1200 an options
// dialog comes apart into 20px slabs — a mosaic, not sand.
export const SURFACE_MOTE_PX = 6;
export const SURFACE_COLS = 46;
export const SURFACE_ROWS = 30;      // 1380 motes; a few thousand promoted layers is lag
// …past that ceiling the CELL is bigger than the grain we want, so the speck inside it is
// capped instead of filling it.
export const SURFACE_SPECK_PX = 7;
export const SURFACE_SPREAD = 34;    // how far a mote may fan off its line to the point
export const SURFACE_FORMING_CLASS = 'surface-forming';
export const SURFACE_LEAVING_CLASS = 'surface-leaving';
// Permanent once a surface has been dusted: its CSS pop must stay off, or dropping the
// forming class at the end of the flight replays it.
export const SURFACE_DRIVEN_CLASS = 'dust-driven';

// One mote's flight when a whole surface gathers into — or bursts out of — a single
// POINT: the path is the cell's offset to it, and the delay rides the DISTANCE, so the
// near edge goes first and the far one last. Pure — unit-tested.
export const surfaceMotion = (cx, cy, cols, rows, box, point, { span = SURFACE_OUT_MS, spread = SURFACE_SPREAD } = {}) => {
  const n = tileNoise(cx, cy);
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  const w = (box?.width || 0) / Math.max(1, cols);
  const h = (box?.height || 0) / Math.max(1, rows);
  const homeX = (box?.left || 0) + (cx + 0.5) * w;
  const homeY = (box?.top || 0) + (cy + 0.5) * h;
  const toX = (point?.x || 0) - homeX;
  const toY = (point?.y || 0) - homeY;
  // Normalised against the longest trip any cell makes, so the sweep fills the flight
  // whatever the point's distance is.
  const far = Math.hypot(box?.width || 0, box?.height || 0) + Math.hypot(toX, toY);
  const progress = far > 0 ? Math.min(1, Math.hypot(toX, toY) / far) : 0;
  const dx = Math.round(toX + (m - 0.5) * spread);
  const dy = Math.round(toY + (n - 0.5) * spread);
  return {
    delay: Math.round(progress * span * 0.45 + n * span * 0.12),
    dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 60).toFixed(2),
    scale: +(0.12 + n * 0.25).toFixed(2),
  };
};

// The centre of the control a surface was opened from. Null (no rect) leaves the caller
// to pick one. Pure.
export const centerOf = (elOrRect) => {
  const r = elOrRect?.getBoundingClientRect ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !Number.isFinite(r.left) || !Number.isFinite(r.top)) return null;
  return { x: r.left + (r.width || 0) / 2, y: r.top + (r.height || 0) / 2 };
};
