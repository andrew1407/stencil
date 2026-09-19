import { TUNE } from './tune.js';
export const CHAT_DISINTEGRATE_COLS = TUNE.CHAT_DISINTEGRATE_COLS;
export const CHAT_DISINTEGRATE_ROWS = TUNE.CHAT_DISINTEGRATE_ROWS;

// A wipe scatters every row at once, so the mesh is budgeted as a total: one row keeps
// the fine grain, a mass clear coarsens each row until the sum fits.
export const SCATTER_TILE_BUDGET = TUNE.SCATTER_TILE_BUDGET;
// Past this many simultaneous rows the extra ones only fade.
export const SCATTER_MAX_ROWS = TUNE.SCATTER_MAX_ROWS;
export const scatterGridFor = (count, index = 0, fine = { cols: CHAT_DISINTEGRATE_COLS, rows: CHAT_DISINTEGRATE_ROWS }) => {
  const n = Math.min(Math.max(1, count | 0), SCATTER_MAX_ROWS);
  if (index >= SCATTER_MAX_ROWS) return { cols: 0, rows: 0 };   // fade only, no dust
  const total = n * fine.cols * fine.rows;
  if (total <= SCATTER_TILE_BUDGET) return fine;
// Same factor on both axes; below the floor the dust reads as broken glass.
  const k = Math.sqrt(SCATTER_TILE_BUDGET / total);
  return { cols: Math.max(8, Math.round(fine.cols * k)), rows: Math.max(4, Math.round(fine.rows * k)) };
};

// Motes fly in a FIXED layer over the page (the row collapsing under them would clip
// them). Desktop twin: DisintegrateOverlay.hpp; a quarter longer than the desktop's.
export const DISINTEGRATE_MS = TUNE.DISINTEGRATE_MS;
// Fine grid: small cells read as ash, not a broken window (desktop: DUST_CELL_PX).
export const DISINTEGRATE_COLS = TUNE.DISINTEGRATE_COLS;
export const DISINTEGRATE_ROWS = TUNE.DISINTEGRATE_ROWS;
// Floor on a late mote's flight, so a short flight's last grains are not a blink.
export const MIN_TILE_MS = TUNE.MIN_TILE_MS;
// The list-item clock, named apart from the row clock (desktop twin: ITEM_MS).
export const ITEM_DUST_MS = DISINTEGRATE_MS;
// Connections rows come and go half again as briskly (desktop twin: CONN_MS).
export const CONN_DUST_MS = Math.round(DISINTEGRATE_MS / 1.5);
// Throw as a share of the row default — the desktop's row ratio.
const ROW_DUST_DRIFT = TUNE.ROW_DUST_DRIFT;
// Finer cells than the row default, still inside SCATTER_TILE_BUDGET.
const ROW_DUST_PX = TUNE.ROW_DUST_PX;
const ROW_DUST_GRID = TUNE.ROW_DUST_GRID;   // 1200 = SCATTER_TILE_BUDGET
// `index` of `count` rows leaving at once.
export const rowDustGrid = (count = 1, index = 0) => ({
  ...scatterGridFor(count, index, ROW_DUST_GRID), px: ROW_DUST_PX,
});
export const rowLeaveDust = (count, index, dustMs) => ({
  ...rowDustGrid(count, index), dustMs, drift: ROW_DUST_DRIFT,
});
// Shares of the span (the CSS defaults' 0.48s and 60ms of 0.9s), so a shorter
// DISINTEGRATE_MS shortens both with it.
// The KEYWORD-CHIP recipe, shared by everything that should read like one: finer motes than
// the surface 3px, a short throw, one clock both ways. Desktop twin: KeywordChipsMotion.cpp.
export const CHIP_MOTE_PX = 1.5;
export const CHIP_DUST_MS = 630;
export const CHIP_DUST_DRIFT = 0.15;
// One budget for every chip flying at once (scatterGridFor caps a LIST at 12 rows).
export const CHIP_TILE_BUDGET = 1380;
export const chipGrid = (sharing = 1) => {
  const per = Math.max(24, Math.floor(CHIP_TILE_BUDGET / Math.max(1, sharing)));
  const cols = Math.max(6, Math.round(Math.sqrt(per * 3)));
  return { cols, rows: Math.max(3, Math.round(per / cols)) };
};

export const TILE_GATHER_SHARE = 480 / 900;
export const TILE_JITTER_SHARE = 60 / 900;

// A hash, not Math.random: varied but reproducible. Returns 0..1.
export const tileNoise = (cx, cy) => {
  const h = Math.sin(cx * 127.1 + cy * 311.7) * 43758.5453;
  return h - Math.floor(h);
};

// Part-way along its throw each mote is bent off its line by a share of the throw, capped
// (desktop twin: DisintegrateOverlay.hpp swirlAt), so a cloud churns instead of radiating.
export const WAYPOINT_ALONG = TUNE.WAYPOINT_ALONG;
export const SWIRL_SHARE = TUNE.SWIRL_SHARE;
export const SWIRL_MAX_PX = TUNE.SWIRL_MAX_PX;
export const tileWaypoint = (dx, dy, q) => {
  const len = Math.hypot(dx, dy);
  if (!(len > 0.5)) return { mx: 0, my: 0 };
  const amp = (q - 0.5) * 2 * Math.min(len * SWIRL_SHARE, SWIRL_MAX_PX);
  return {
    mx: Math.round(dx * WAYPOINT_ALONG - (dy / len) * amp),
    my: Math.round(dy * WAYPOINT_ALONG + (dx / len) * amp),
  };
};

// `reverse` inverts only the sweep (the gather): first out is last home. `drift` scales
// the throw (desktop: controlSwap.hpp CHECK_SWAP_SPREAD); `span` is the flight's own
// length — the sweep and jitter are shares of it.
export const tileMotion = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS, reverse = false, drift = 1,
                           span = DISINTEGRATE_MS) => {
  const n = tileNoise(cx, cy);
// Decorrelated noises: one hash for fall, drift and spin makes whole diagonals move as one.
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  const progress = rows > 1 ? cy / (rows - 1) : 0;
// A scatter's sweep is half the gather's: past LEAVE_MS a mote still at its 0% pose is a
// dot screen where the row was (the same halving as surfaceMotion's delayScale).
  const sweep = span * (reverse ? 0.4 : 0.2);
  const delay = Math.round((reverse ? 1 - progress : progress) * sweep + n * span * TILE_JITTER_SHARE);
  const dx = Math.round((m - 0.5) * 66 * drift);
  const dy = Math.round((26 + progress * 30 + n * 44) * drift);
  return {
    delay, dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 70).toFixed(2),
    scale: +(0.3 + n * 0.3).toFixed(2),
  };
};

// Motes sized in pixels: aim for MOTE_PX, the quoted grid's cell count is the ceiling
// (desktop: DUST_CELL_PX).
export const MOTE_PX = TUNE.MOTE_PX;
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

export function cancelDust(el) {
  if (!el) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__dustTimer);
  el.__dustTimer = null;
  el.__dustHost?.__stop?.();   // the canvas loop, before the layer it paints goes
  el.__dustHost?.remove?.();
  el.__dustHost = null;
}

// Take down every cloud that came out of `scope` (a modal overlay): clouds are parented
// to <body>, so closing the window otherwise leaves them flying with nothing behind them.
export function sweepDust(scope) {
  const id = typeof scope === 'string' ? scope : scope?.id;
  if (!id || typeof document === 'undefined') return 0;
// Matched by data, not an attribute selector: CSS.escape does not exist in the node suite.
  let swept = 0;
  for (const host of document.querySelectorAll('.disintegrate-host')) {
    if (host.dataset?.dustScope !== id) continue;
    host.__stop?.();
    host.remove();
    swept++;
  }
  return swept;
}

// The host's left/top are pinned at creation; a layout shift afterwards strands the cloud.
export function retargetDust(el, r = null) {
  if (!el?.__dustHost || !el.getBoundingClientRect) return;
  r = r || el.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return;
  el.__dustHost.style.left = `${r.left}px`;
  el.__dustHost.style.top = `${r.top}px`;
}

export const flightOf = (toward, gather, flight) => flight
  || (toward ? (gather ? 'surfaceGather' : 'surfaceScatter') : (gather ? 'gather' : 'scatter'));
