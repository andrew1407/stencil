// ── The grids and the per-tile maths every cloud is built from ──────────────
// Mirror of browser js/ui/motion/surface/tiles.js.

// A chat entry comes apart in a finer grid than a list row (browser twin).
export const CHAT_DISINTEGRATE_COLS = 32;
export const CHAT_DISINTEGRATE_ROWS = 16;

// A mass clear coarsens each row until the summed cost fits. Pure.
export const SCATTER_TILE_BUDGET = 1200;   // browser motion.js twin
// Past this many simultaneous rows the rest fade: no budget survives 200 of them.
export const SCATTER_MAX_ROWS = 12;
export const scatterGridFor = (count, index = 0) => {
  const n = Math.min(Math.max(1, count | 0), SCATTER_MAX_ROWS);
  if (index >= SCATTER_MAX_ROWS) return { cols: 0, rows: 0 };   // fade only, no dust
  const fine = { cols: CHAT_DISINTEGRATE_COLS, rows: CHAT_DISINTEGRATE_ROWS };
  const total = n * fine.cols * fine.rows;
  if (total <= SCATTER_TILE_BUDGET) return fine;
  // Square-ish cells; below the floor the dust reads as broken glass.
  const k = Math.sqrt(SCATTER_TILE_BUDGET / total);
  return { cols: Math.max(8, Math.round(fine.cols * k)), rows: Math.max(4, Math.round(fine.rows * k)) };
};

export const DISINTEGRATE_MS = 1350;
// At 8x4 the cells read as rectangles sliding apart (browser DISINTEGRATE_*).
export const DISINTEGRATE_COLS = 34;
export const DISINTEGRATE_ROWS = 16;
// The last grains still fly this long, never a blink (browser twin).
export const MIN_TILE_MS = 160;
// Shares of the span, so shortening DISINTEGRATE_MS shortens both.
export const TILE_GATHER_SHARE = 480 / 900;
export const TILE_JITTER_SHARE = 60 / 900;

// A hash, not Math.random, so the scatter is reproducible. 0..1.
export const tileNoise = (cx, cy) => {
  const h = Math.sin(cx * 127.1 + cy * 311.7) * 43758.5453;
  return h - Math.floor(h);
};

// Each mote is pushed off its line by its own amount, so a cloud churns rather than radiating.
export const WAYPOINT_ALONG = 0.62;
export const SWIRL_SHARE = 0.32;
export const SWIRL_MAX_PX = 44;
export const tileWaypoint = (dx, dy, q) => {
  const len = Math.hypot(dx, dy);
  if (!(len > 0.5)) return { mx: 0, my: 0 };
  const amp = (q - 0.5) * 2 * Math.min(len * SWIRL_SHARE, SWIRL_MAX_PX);
  return {
    mx: Math.round(dx * WAYPOINT_ALONG - (dy / len) * amp),
    my: Math.round(dy * WAYPOINT_ALONG + (dx / len) * amp),
  };
};

// `reverse` inverts only the sweep; sweep and jitter are shares of `span`. Pure.
export const tileMotion = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS, reverse = false,
                           span = DISINTEGRATE_MS) => {
  const n = tileNoise(cx, cy);
  // Decorrelated noises: one hash for drift, fall and spin tears the row as a sheet.
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  // 0 at the top row (goes first), 1 at the bottom.
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  // A scatter's sweep is half the gather's: the row is gone in LEAVE_MS.
  const sweep = span * (reverse ? 0.4 : 0.2);
  const delay = Math.round((reverse ? 1 - progress : progress) * sweep + n * span * TILE_JITTER_SHARE);
  // Signed drift, so the falling motes fan both ways.
  const dx = Math.round((m - 0.5) * 66);
  const dy = Math.round(26 + progress * 30 + n * 44);
  return {
    delay, dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 70).toFixed(2),
    scale: +(0.3 + n * 0.3).toFixed(2),
  };
};

// Sized in pixels; the quoted grid's cell count is the frame-budget ceiling. Pure.
export const MOTE_PX = 7;
export const reshapeGrid = (cols, rows, w, h, px = MOTE_PX) => {
  const budget = Math.max(1, cols * rows);
  // At least three bands each way: two reads as a thing splitting in half, not crumbling.
  let c = Math.max(3, Math.round((w || 1) / px));
  let r = Math.max(3, Math.round((h || 1) / px));
  if (c * r > budget) {
    const k = Math.sqrt(budget / (c * r));
    c = Math.max(3, Math.round(c * k));
    r = Math.max(3, Math.round(r * k));
  }
  return { cols: c, rows: r };
};

// The host's left/top are pinned at launch, so a scroll would strand it (browser motion.js twin).
export function retargetDust(el) {
  if (!el?.__dustHost || !el.getBoundingClientRect) return;
  const r = el.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return;
  el.__dustHost.style.left = `${r.left}px`;
  el.__dustHost.style.top = `${r.top}px`;
}

// A superseding open/close calls this, so a double-click never strands a cloud.
export function cancelDust(el) {
  if (!el) return;
  clearTimeout(el.__dustTimer);
  el.__dustTimer = null;
  el.__dustHost?.__stop?.();   // the canvas loop, before the layer it paints goes
  el.__dustHost?.remove?.();
  el.__dustHost = null;
}

export const flightOf = (toward, gather) =>
  toward ? (gather ? 'surfaceGather' : 'surfaceScatter') : (gather ? 'gather' : 'scatter');
