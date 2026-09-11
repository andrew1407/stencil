import { tileNoise, tileWaypoint } from './tiles.js';
import { TUNE } from './tune.js';
// ── Surfaces: a window, a panel and a mini popup are dust too ───────────────
// A modal, the chat panel and the ⋯/context popups play the SAME scatter a deleted
// row does — only every mote flies INTO (or out of) the point that owns the surface:
// the icon that opened it, the click a context menu grew from, the edge a docked
// panel slides off. Origin and direction are exactly what the old scale had; what
// changed is that the flight is rendered as particles instead of a moving rectangle.
// The way IN is the slower half on purpose: a window forming is the thing you watch,
// and it has to arrive gently enough to read as sand gathering rather than a flash.
// Going out is brisk — you have already decided.
export const SURFACE_IN_MS = TUNE.SURFACE_IN_MS;
export const SURFACE_OUT_MS = TUNE.SURFACE_OUT_MS;   // ui/base.js CLOSE_MS rides this
// A MENU is not a window: it is opened to be clicked, often blind, so it may not spend
// half a second forming. Its own, brisker clock — the flight is the same one.
export const SURFACE_MENU_IN_MS = TUNE.SURFACE_MENU_IN_MS;
export const SURFACE_MENU_OUT_MS = TUNE.SURFACE_MENU_OUT_MS;
// The grain a mote AIMS for, and the mote-budget ceiling. The ceiling was 672 when a
// mote was a compositor layer of its own; on one canvas (dustCloud.js) a grain costs a
// few arcs, so it now matches the extension's and the desktop's (kSurfaceMaxCells).
// The speck is still sized separately (SURFACE_SPECK_PX) — air between grains is sand.
export const SURFACE_MOTE_PX = TUNE.SURFACE_MOTE_PX;
export const SURFACE_COLS = TUNE.SURFACE_COLS;
export const SURFACE_ROWS = TUNE.SURFACE_ROWS;      // 1380 motes
// …and past that ceiling the CELL is bigger than the grain we want, so the speck drawn
// inside it is capped instead of filling it. What you see is the speck, not the cell.
export const SURFACE_SPECK_PX = TUNE.SURFACE_SPECK_PX;
export const SURFACE_SPREAD = TUNE.SURFACE_SPREAD;    // how far a mote may fan off its line to the point
export const SURFACE_FORMING_CLASS = 'surface-forming';
export const SURFACE_LEAVING_CLASS = 'surface-leaving';
// Permanent once a surface has been dusted: its old CSS pop/slide must stay off for
// good, or removing the forming class at the end of the flight would replay it.
export const SURFACE_DRIVEN_CLASS = 'dust-driven';

// One mote's flight when a whole surface gathers into — or bursts out of — a single
// POINT. The path is the cell's own offset to that point, so every mote converges
// there instead of falling; the two decorrelated noises only fan the arrival. The
// delay rides the DISTANCE, so the edge nearest the point goes first and the far one
// last — a window draining into its icon, and pouring back out of it. Pure.
// `delayScale` halves that stagger for a SCATTER (disintegrate's own `gather: false`):
// "going out is brisk" was already the rule for the span, but the sweep itself still
// held every mote at its 0% pose (still, opaque — indistinguishable from the surface
// not having reacted yet) for up to 45% of it. On a big surface that reads as nothing
// moving at all until a sudden, late flick — a blink, not sand leaving.
export const surfaceMotion = (cx, cy, cols, rows, box, point,
                              { span = SURFACE_OUT_MS, spread = SURFACE_SPREAD, delayScale = 1 } = {}) => {
  const n = tileNoise(cx, cy);
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  const w = (box?.width || 0) / Math.max(1, cols);
  const h = (box?.height || 0) / Math.max(1, rows);
  const homeX = (box?.left || 0) + (cx + 0.5) * w;
  const homeY = (box?.top || 0) + (cy + 0.5) * h;
  const toX = (point?.x || 0) - homeX;
  const toY = (point?.y || 0) - homeY;
  // Normalised against the longest trip any cell in this box makes, so the sweep fills
  // the whole flight whatever the point's distance is.
  const far = Math.hypot(box?.width || 0, box?.height || 0) + Math.hypot(toX, toY);
  const progress = far > 0 ? Math.min(1, Math.hypot(toX, toY) / far) : 0;
  const dx = Math.round(toX + (m - 0.5) * spread);
  const dy = Math.round(toY + (n - 0.5) * spread);
  return {
    delay: Math.round((progress * span * 0.45 + n * span * 0.12) * delayScale),
    dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 60).toFixed(2),
    scale: +(0.12 + n * 0.25).toFixed(2),
  };
};

// Where a DOCKED panel's dust comes from, and goes back to: far out past the edge it
// is docked to, so the motes stream along the panel's own slide direction — the same
// "where it comes from" the slide had. A float has an icon instead, so: null. `reach`
// is measured from the panel's own CENTRE, not its edge — 1.7 lands the point 1.2
// panel-widths past the edge itself, matching the desktop overlay's own reachPx
// (mainWindow.cpp chatSurfaceFlight: `picture.width() * 1.2`). Pure.
export const dockAwayPoint = (rect, dock, reach = 1.7) => {
  if (!rect) return null;
  const cx = rect.left + rect.width / 2;
  const cy = rect.top + rect.height / 2;
  if (dock === 'left') return { x: cx - rect.width * reach, y: cy };
  if (dock === 'right') return { x: cx + rect.width * reach, y: cy };
  if (dock === 'top') return { x: cx, y: cy - rect.height * reach };
  if (dock === 'bottom') return { x: cx, y: cy + rect.height * reach };
  return null;
};

// Is `c` a colour that paints nothing? An unset background, or a fully transparent one.
