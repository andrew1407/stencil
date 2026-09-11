import { TUNE } from './tune.js';
export const CHAT_DISINTEGRATE_COLS = TUNE.CHAT_DISINTEGRATE_COLS;
export const CHAT_DISINTEGRATE_ROWS = TUNE.CHAT_DISINTEGRATE_ROWS;

// A WIPE scatters every row at once and the cost is the sum, not the per-row grid —
// so the mesh is budgeted: one row keeps the full fine grain, a mass clear coarsens
// each row until the total fits. Pure — unit-tested.
export const SCATTER_TILE_BUDGET = TUNE.SCATTER_TILE_BUDGET;
// …and past this many simultaneous rows the extra ones simply fade: a dozen scatters
// at once is already more than the eye resolves, and 200 of them would blow any mesh
// budget however coarse it got.
export const SCATTER_MAX_ROWS = TUNE.SCATTER_MAX_ROWS;
// `fine` is the one-row grid to start from — the chat's by default; the lists bring a
// finer one (ROW_DUST_GRID), still under the same total budget.
export const scatterGridFor = (count, index = 0, fine = { cols: CHAT_DISINTEGRATE_COLS, rows: CHAT_DISINTEGRATE_ROWS }) => {
  const n = Math.min(Math.max(1, count | 0), SCATTER_MAX_ROWS);
  if (index >= SCATTER_MAX_ROWS) return { cols: 0, rows: 0 };   // fade only, no dust
  const total = n * fine.cols * fine.rows;
  if (total <= SCATTER_TILE_BUDGET) return fine;
  // Scale both axes by the same factor, so the cells stay square-ish, and keep a floor
  // — below it the "dust" reads as broken glass instead.
  const k = Math.sqrt(SCATTER_TILE_BUDGET / total);
  return { cols: Math.max(8, Math.round(fine.cols * k)), rows: Math.max(4, Math.round(fine.rows * k)) };
};

// ── Disintegration ("the snap") ─────────────────────────────────────────────
// A removed element comes apart into MOTES: one round speck per grid cell, painted in
// the element's own colours (speckPainter), drifting off in a staggered sweep. Never
// clones of the element — a clone per cell was hundreds of copies of a row's whole
// subtree, and at mote size it showed nothing a speck does not. The motes live in a
// FIXED layer over the page: the row is collapsing to zero height at the same time and
// would clip anything inside it. Every flight here — a row, a chat entry, a window, a
// tick — is this one particle system (desktop twin: disintegrateOverlay.hpp).
// Half again the app's original 900ms wipe: 520 and 260 were both tried for briskness
// and read as hurried over anything you are still looking at.
// Every dust clock in this file runs about a quarter longer than the desktop's twin of
// it: the browser's grains are flat specks where the desktop flies the window's own
// pixels, and the same span read as hurried here (user report, 2026-09-08).
export const DISINTEGRATE_MS = TUNE.DISINTEGRATE_MS;
// A fine grid — small cells read as ash rather than a broken window; one node per
// cell, so this is the practical ceiling for a list row. (Matched by the desktop's
// DisintegrateOverlay::kDustCellPx, which sizes its motes in pixels instead.)
export const DISINTEGRATE_COLS = TUNE.DISINTEGRATE_COLS;
export const DISINTEGRATE_ROWS = TUNE.DISINTEGRATE_ROWS;
// However late a mote sets off, it still gets this long to fly: the floor keeps the last
// grains of a short flight (a mark swap, a menu) from being a blink rather than a flight.
export const MIN_TILE_MS = TUNE.MIN_TILE_MS;
// The clock a list passes for its own items — the row clock today, named apart because
// the two have been parted before (desktop twin: kItemMs).
export const ITEM_DUST_MS = DISINTEGRATE_MS;
// …and the CONNECTIONS list, the odd one out: a row there is a URL you already know, so
// it comes and goes half again as briskly (desktop twin: kConnMs).
export const CONN_DUST_MS = Math.round(DISINTEGRATE_MS / 1.5);
// ── A LIST ROW's dust: the connections and projects lists ───────────────────
// The throw, as a share of the row default. That default is a fixed pixel count, so over
// a 44px row it is two and a half times its height; this is the desktop's row ratio.
const ROW_DUST_DRIFT = TUNE.ROW_DUST_DRIFT;
// …and the grain: at the row default a short row holds a mosaic of big dots rather than
// sand (user report). Finer cells, twice as many, still inside SCATTER_TILE_BUDGET.
const ROW_DUST_PX = TUNE.ROW_DUST_PX;
const ROW_DUST_GRID = TUNE.ROW_DUST_GRID;   // 1200 = SCATTER_TILE_BUDGET
// The grain and budgeted grid every list row's dust shares (connections + projects) —
// `index` of `count` rows leaving at once. An ARRIVAL keeps materialize's own clock and
// throw, so it takes this alone; a REMOVAL adds the list's clock and the row throw.
export const rowDustGrid = (count = 1, index = 0) => ({
  ...scatterGridFor(count, index, ROW_DUST_GRID), px: ROW_DUST_PX,
});
export const rowLeaveDust = (count, index, dustMs) => ({
  ...rowDustGrid(count, index), dustMs, drift: ROW_DUST_DRIFT,
});
// A gathering tile's flight, and a mote's own jitter, as SHARES of the span (the CSS
// defaults' 0.48s and 60ms of 0.9s) — not divisions by it: shortening DISINTEGRATE_MS
// must shorten both with it, not hand them a bigger slice of a smaller flight.
export const TILE_GATHER_SHARE = 480 / 900;
export const TILE_JITTER_SHARE = 60 / 900;

// Deterministic per-tile jitter — a hash, not Math.random, so the scatter is varied
// but reproducible (and unit-testable). Returns a 0..1 float.
export const tileNoise = (cx, cy) => {
  const h = Math.sin(cx * 127.1 + cy * 311.7) * 43758.5453;
  return h - Math.floor(h);
};

// ── The waypoint: no mote flies a straight line ─────────────────────────────
// Part-way along its throw each mote is pushed off its line by its own amount, to its
// own side — a bend, not a beam — so a cloud churns instead of radiating in spokes.
// CSS plays it as the mid keyframe (--mx/--my; animations.css tileScatter and kin),
// the desktop as a sine bulge on the same throw (disintegrateOverlay.hpp swirlAt).
// The push is a share of the throw, capped: a window's 400px trip must not swing its
// motes across half the page. Pure — unit-tested.
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

// Where a cell goes and when it starts. `reverse` inverts only the SWEEP (for the
// gather — see reintegrate): the mote that leaves first is the last one home, the same
// rule the canvas dust follows (dustDelay); the flight path is shared. Pure.
// `drift` scales the throw: a list row flings its motes tens of pixels, and a 15px
// checkbox indicator doing the same would read as the dialog exploding (the desktop's
// controlSwap.hpp kCheckSwapSpread is this number).
// `span` is the flight's own length: the sweep and its jitter are SHARES of it, so a
// chat entry arriving on a shorter clock (CHAT_ENTER_MS) still lands every mote in time.
export const tileMotion = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS, reverse = false, drift = 1,
                           span = DISINTEGRATE_MS) => {
  const n = tileNoise(cx, cy);
  // A second, decorrelated noise so a mote's SIDEWAYS drift is independent of its fall
  // and its spin — one hash drove all three, which made whole diagonals move as one and
  // read as a sheet tearing rather than a thing coming apart. A third bends the path.
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  // 0 at the TOP row (goes first), 1 at the bottom (goes last): the row crumbles from
  // its top edge downward, the way the cleared image does (ghostOut).
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  // A SCATTER's sweep is half the gather's: the row itself is gone in LEAVE_MS, and a
  // mote still at its 0% pose past that is a dot screen sitting where the row was, not
  // sand leaving (the same halving surfaceMotion's delayScale does). The gather keeps
  // the full sweep — its motes are the row forming, and there is nothing under them.
  const sweep = span * (reverse ? 0.4 : 0.2);
  const delay = Math.round((reverse ? 1 - progress : progress) * sweep + n * span * TILE_JITTER_SHARE);
  // …and the motes FALL, fanning out as they go. Signed drift, so they spread both
  // ways instead of all sliding one.
  const dx = Math.round((m - 0.5) * 66 * drift);
  const dy = Math.round((26 + progress * 30 + n * 44) * drift);
  return {
    delay, dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 70).toFixed(2),
    scale: +(0.3 + n * 0.3).toFixed(2),
  };
};

// Motes sized in PIXELS, not as a share of the element — a fixed grid over a wide row
// gives slivers, over a small card gives real dust. Aim for MOTE_PX; the quoted grid's
// cell COUNT is the frame-budget ceiling. Pure — unit-tested. (Desktop: kDustCellPx.)
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

// Drop the dust layer an element still owns, if any. A superseding open/close calls
// this, so a double-clicked menu never strands a cloud over the page.
export function cancelDust(el) {
  if (!el) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__dustTimer);
  el.__dustTimer = null;
  el.__dustHost?.__stop?.();   // the canvas loop, before the layer it paints goes
  el.__dustHost?.remove?.();
  el.__dustHost = null;
}

// Take down every cloud that came out of `scope` (a modal overlay), whoever owns it.
// A cloud is parented to <body> to clear the scroller, so closing the window it belongs
// to leaves it flying over the page — motes with nothing behind them (user report:
// remove a project, close the window, the removal is still coming apart mid-air). The
// window's OWN close flight is started after this, so it is never swept with them.
export function sweepDust(scope) {
  const id = typeof scope === 'string' ? scope : scope?.id;
  if (!id || typeof document === 'undefined') return 0;
  // Matched by DATA, not by an attribute selector: an id is arbitrary text, and CSS.escape
  // does not exist in every environment this module is loaded in (the node suite has no DOM).
  let swept = 0;
  for (const host of document.querySelectorAll('.disintegrate-host')) {
    if (host.dataset?.dustScope !== id) continue;
    host.__stop?.();
    host.remove();
    swept++;
  }
  return swept;
}

// Re-anchor a still-flying cloud to `el`'s CURRENT box. The host's left/top are pinned
// once, at creation (disintegrate below) — a layout change that moves `el` afterward (a
// sibling toast pushing it up the stack) leaves the cloud stranded at the old spot while
// the real element reappears somewhere else with no motes to show for it. A no-op when
// `el` owns no live cloud.
export function retargetDust(el, r = null) {
  if (!el?.__dustHost || !el.getBoundingClientRect) return;
  r = r || el.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return;
  el.__dustHost.style.left = `${r.left}px`;
  el.__dustHost.style.top = `${r.top}px`;
}

// Which of dustCloud's FLIGHTS a cloud flies: a surface's aimed gather/scatter, a row's
// fall-and-fan (or its reverse), or a mark's own fall (`flight: 'fall'`, markOut).
export const flightOf = (toward, gather, flight) => flight
  || (toward ? (gather ? 'surfaceGather' : 'surfaceScatter') : (gather ? 'gather' : 'scatter'));
