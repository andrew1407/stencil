import { tileNoise, tileWaypoint } from './tiles.js';
import { TUNE } from './tune.js';
// A modal, the chat panel and the popups play a deleted row's scatter, every mote flying
// into (or out of) the point that owns the surface. The way IN is the slower half.
export const SURFACE_IN_MS = TUNE.SURFACE_IN_MS;
export const SURFACE_OUT_MS = TUNE.SURFACE_OUT_MS;   // ui/base.js CLOSE_MS rides this
// A menu is opened to be clicked, so it may not spend half a second forming.
export const SURFACE_MENU_IN_MS = TUNE.SURFACE_MENU_IN_MS;
export const SURFACE_MENU_OUT_MS = TUNE.SURFACE_MENU_OUT_MS;
// The grain a mote aims for, and the mote ceiling — matching the extension's and the
// desktop's (SURFACE_MAX_CELLS). The speck is sized separately: air between grains is sand.
export const SURFACE_MOTE_PX = TUNE.SURFACE_MOTE_PX;
export const SURFACE_COLS = TUNE.SURFACE_COLS;
export const SURFACE_ROWS = TUNE.SURFACE_ROWS;      // 1380 motes
// Past the ceiling the cell is bigger than the grain, so the speck is capped, not the cell.
export const SURFACE_SPECK_PX = TUNE.SURFACE_SPECK_PX;
export const SURFACE_SPREAD = TUNE.SURFACE_SPREAD;    // how far a mote may fan off its line to the point
export const SURFACE_FORMING_CLASS = 'surface-forming';
export const SURFACE_LEAVING_CLASS = 'surface-leaving';
// Permanent once dusted: the surface's old CSS pop/slide must stay off for good.
export const SURFACE_DRIVEN_CLASS = 'dust-driven';

// One mote's flight into or out of a point: the path is the cell's offset to it, the
// delay rides the distance (nearest edge first). `delayScale` halves the stagger for a
// scatter — a big surface otherwise sits still for 45% of it, then flicks.
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
// Normalised against the longest trip any cell makes, so the sweep fills the whole flight.
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

// Where a docked panel's dust comes from: far past the edge it is docked to, along its
// slide direction. `reach` is from the panel's CENTRE — 1.7 lands 1.2 widths past the
// edge, matching the desktop (MainWindow.cpp chatSurfaceFlight `picture.width() * 1.2`).
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

