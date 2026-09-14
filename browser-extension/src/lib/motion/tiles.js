// ── The grids and the per-tile maths every cloud is built from ──────────────
// Mirror of browser js/ui/motion/tiles.js.

// A chat entry comes apart in a finer grid than a list row (browser twin).
export const CHAT_DISINTEGRATE_COLS = 32;
export const CHAT_DISINTEGRATE_ROWS = 16;

// A wipe scatters every row at once and the cost is the sum, not the per-row grid, so
// the mesh is budgeted: one row keeps the fine grain, a mass clear coarsens each row
// until the total fits. Pure — unit-tested.
export const SCATTER_TILE_BUDGET = 1200;   // browser motion.js twin
// …and past this many simultaneous rows the extra ones simply fade: no mesh budget,
// however coarse, survives 200 of them.
export const SCATTER_MAX_ROWS = 12;
export const scatterGridFor = (count, index = 0) => {
  const n = Math.min(Math.max(1, count | 0), SCATTER_MAX_ROWS);
  if (index >= SCATTER_MAX_ROWS) return { cols: 0, rows: 0 };   // fade only, no dust
  const fine = { cols: CHAT_DISINTEGRATE_COLS, rows: CHAT_DISINTEGRATE_ROWS };
  const total = n * fine.cols * fine.rows;
  if (total <= SCATTER_TILE_BUDGET) return fine;
  // Both axes by the same factor so cells stay square-ish; below the floor the "dust"
  // reads as broken glass.
  const k = Math.sqrt(SCATTER_TILE_BUDGET / total);
  return { cols: Math.max(8, Math.round(fine.cols * k)), rows: Math.max(4, Math.round(fine.rows * k)) };
};

export const DISINTEGRATE_MS = 1350;
// A fine grid: at 8x4 the cells read as big rectangles sliding apart, not as ash
// (browser DISINTEGRATE_*).
export const DISINTEGRATE_COLS = 34;
export const DISINTEGRATE_ROWS = 16;
// However late a mote sets off, it still gets this long to fly: the floor keeps the last
// grains of a short flight from being a blink rather than a flight (browser twin).
export const MIN_TILE_MS = 160;
// A gathering tile's flight and a mote's jitter as SHARES of the span, not divisions by
// it: shortening DISINTEGRATE_MS must shorten both with it.
export const TILE_GATHER_SHARE = 480 / 900;
export const TILE_JITTER_SHARE = 60 / 900;

// Deterministic per-tile jitter — a hash, not Math.random, so the scatter is varied
// but reproducible (and unit-testable). Returns a 0..1 float.
export const tileNoise = (cx, cy) => {
  const h = Math.sin(cx * 127.1 + cy * 311.7) * 43758.5453;
  return h - Math.floor(h);
};

// ── The waypoint: no mote flies a straight line (browser motion.js twin) ────
// Part-way along its throw each mote is pushed off its line by its own amount, to its own
// side, so a cloud churns instead of radiating in spokes. CSS plays it as the mid keyframe
// (--mx/--my; animations/reveal.css stTileScatter and kin). Pure — unit-tested.
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

// Where a cell goes and when it starts. The sweep erodes the element from one edge and
// every cell drifts, further the later it goes. `reverse` inverts only the SWEEP (the
// gather — see reintegrate); the path is shared, played backwards. `span` is the flight's
// length, and the sweep and jitter are SHARES of it, so a shorter clock still lands. Pure.
export const tileMotion = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS, reverse = false,
                           span = DISINTEGRATE_MS) => {
  const n = tileNoise(cx, cy);
  // Decorrelated noises: one hash driving drift, fall and spin moves whole diagonals as
  // one and reads as a sheet tearing. A third bends the path.
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  // 0 at the TOP row (goes first), 1 at the bottom (goes last): the row crumbles from
  // its top edge downward, the way the cleared image does.
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  // A SCATTER's sweep is half the gather's: the row is gone in LEAVE_MS, and a mote still
  // at its 0% pose past that reads as a dot screen sitting where the row was. The gather
  // keeps the full sweep — its motes ARE the row forming.
  const sweep = span * (reverse ? 0.4 : 0.2);
  const delay = Math.round((reverse ? 1 - progress : progress) * sweep + n * span * TILE_JITTER_SHARE);
  // …and the motes FALL, fanning out as they go. Signed drift, so they spread both
  // ways instead of all sliding one.
  const dx = Math.round((m - 0.5) * 66);
  const dy = Math.round(26 + progress * 30 + n * 44);
  return {
    delay, dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 70).toFixed(2),
    scale: +(0.3 + n * 0.3).toFixed(2),
  };
};

// Motes sized in PIXELS, not as a share of the element — a fixed grid gives slivers on a
// wide row. Aim for MOTE_PX; the quoted grid's cell COUNT is the frame-budget ceiling.
// Pure — unit-tested (browser motion.js twin).
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

// Re-anchor a still-flying cloud to `el`'s CURRENT box (browser motion.js twin): the
// host's left/top are pinned once, at launch, so a scroll strands it at the old spot.
export function retargetDust(el) {
  if (!el?.__dustHost || !el.getBoundingClientRect) return;
  const r = el.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return;
  el.__dustHost.style.left = `${r.left}px`;
  el.__dustHost.style.top = `${r.top}px`;
}

// Drop the dust layer an element still owns. A superseding open/close calls this, so a
// double-clicked menu never strands a cloud over the page.
export function cancelDust(el) {
  if (!el) return;
  clearTimeout(el.__dustTimer);
  el.__dustTimer = null;
  el.__dustHost?.__stop?.();   // the canvas loop, before the layer it paints goes
  el.__dustHost?.remove?.();
  el.__dustHost = null;
}

// Which of dustCloud's FLIGHTS a cloud flies: a surface's aimed gather/scatter, or a
// row's fall-and-fan (and its reverse).
export const flightOf = (toward, gather) =>
  toward ? (gather ? 'surfaceGather' : 'surfaceScatter') : (gather ? 'gather' : 'scatter');
