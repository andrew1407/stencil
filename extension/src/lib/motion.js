// ── Shared UI motion helpers (mirror of browser/js/ui/motion.js) ────────────
// Pure decoration: without an IntersectionObserver/MutationObserver nothing here
// runs and every list simply shows normally. CSS owns the keyframes
// (lib/animations.css); this file only toggles classes.

// Rows only dissolve by the amount the scroller is ALREADY clipping them — a row you
// can see in full is never touched. Decoration must never cost legibility (the grain
// is finer than a glyph's strokes), so only the part already being cut off dissolves.
export const REVEAL_ITEM_CLASS = 'reveal-item';
export const REVEAL_IN_CLASS = 'reveal-in';
export const REVEAL_ENTERING_CLASS = 'reveal-entering';
// Gates the mask itself: only a row straddling an edge is worth masking.
export const REVEAL_MASKED_CLASS = 'reveal-masked';
export const REVEAL_ENTER_MS = 700;

// How dissolved a row spanning [top, bottom) is in a scroller `viewH` tall: 0 while
// wholly on screen, rising with the clipped share, 1 once it is gone. Pure.
export const revealDissolve = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / h;
};

// Grain for a row. The dot screen covers the whole element, so the clipped share won't
// do: a row taller than the scroller is clipped by definition and would stay speckled.
// Showing as much as the viewport holds counts as fully visible. Pure.
export const revealGrain = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / Math.min(h, viewH);
};

// Watch `root` and ramp every child matching `selector` as it scrolls. Returns a
// disconnect function; safe in any environment. Scrolling must stay cheap: geometry is
// measured once per list change (per-frame getBoundingClientRect forces a layout and
// stutters), and the mask is mounted only on rows actually straddling an edge.
export function observeReveal(root, selector) {
  const noop = () => {};
  if (!root?.addEventListener || typeof requestAnimationFrame === 'undefined') return noop;

  const seen = new WeakSet();
  let rows = [];        // cached geometry: { el, top, h } in the scroller's content space
  let raf = 0;

  // One layout read for the whole list, and only when the list itself changed.
  const measure = () => {
    const rootTop = root.getBoundingClientRect().top - root.scrollTop;
    rows = [...root.querySelectorAll(selector)].map((el) => {
      const r = el.getBoundingClientRect();
      return { el, top: r.top - rootTop, h: r.height };
    });
  };

  const apply = () => {
    raf = 0;
    const viewH = root.clientHeight;
    const scrollTop = root.scrollTop;
    for (const row of rows) {
      const top = row.top - scrollTop;             // pure arithmetic — no layout read
      const bottom = top + row.h;
      const d = revealDissolve(top, bottom, viewH);
      // Only a row straddling an edge needs the mask; everything else is either fully
      // readable or fully clipped away by the scroller.
      const masked = d > 0.001 && d < 0.999;
      if (masked !== row.masked) {
        row.masked = masked;
        row.el.classList.toggle(REVEAL_MASKED_CLASS, masked);
      }
      if (!masked) continue;
      if (row.h > 0) {
        row.el.style.setProperty('--vis-start', `${(Math.min(1, Math.max(0, -top / row.h)) * 100).toFixed(2)}%`);
        row.el.style.setProperty('--vis-end', `${(Math.min(1, Math.max(0, (viewH - top) / row.h)) * 100).toFixed(2)}%`);
      }
      // The soft edge (--vis-*) follows the clipping; the grain has its own ramp so a
      // long message stays readable while you are reading it.
      row.el.style.setProperty('--dissolve', revealGrain(top, bottom, viewH).toFixed(3));
    }
  };
  const schedule = () => { if (!raf) raf = requestAnimationFrame(apply); };
  const remeasure = () => { measure(); schedule(); };

  // New rows start dissolved and carry a LONGER transition for their first ramp, so an
  // arriving message assembles visibly instead of tracking the scroll instantly.
  const scan = () => {
    for (const el of root.querySelectorAll(selector)) {
      if (seen.has(el)) continue;
      seen.add(el);
      el.classList.add(REVEAL_ITEM_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS);
      el.style.setProperty('--dissolve', '1');
      el.style.setProperty('--vis-start', '50%');   // assembles outward from its middle
      el.style.setProperty('--vis-end', '50%');
      setTimeout(() => el.classList.remove(REVEAL_ENTERING_CLASS), REVEAL_ENTER_MS + 60);
    }
    remeasure();
  };
  scan();

  root.addEventListener('scroll', schedule, { passive: true });
  let mo = null;
  if (typeof MutationObserver !== 'undefined') {
    mo = new MutationObserver(scan);
    mo.observe(root, { childList: true, subtree: true });
  }
  let ro = null;
  if (typeof ResizeObserver !== 'undefined') {
    ro = new ResizeObserver(remeasure);   // the scroller resized: every cached top moved
    ro.observe(root);
  }
  return () => {
    root.removeEventListener('scroll', schedule);
    mo?.disconnect();
    ro?.disconnect();
  };
}

// ── Leaving ─────────────────────────────────────────────────────────────────
// A row about to be destroyed collapses and fades out first, so a delete reads as
// the row going away rather than the list jumping. The caller does the actual
// removal in the callback — this only buys it the time. Mirrors the browser twin.
export const LEAVE_MS = 220;
// Chat entries leave more slowly and in a finer grid than a list row (browser
// motion.js twin): a message is something you deleted on purpose, and the extra time
// + particles are what make that read as "it dissolved" rather than "it blinked out".
export const CHAT_LEAVE_MS = 260;
export const CHAT_DISINTEGRATE_COLS = 32;
export const CHAT_DISINTEGRATE_ROWS = 16;
export const LEAVING_CLASS = 'leaving';

// A single removal scatters ONE element, but a WIPE scatters every row at once — and
// the cost is the sum, not the per-row grid. So the mesh is budgeted: one row keeps the
// full fine grain, a mass clear coarsens each row until the total fits. Pure — unit-tested.
export const SCATTER_TILE_BUDGET = 700;
// …and past this many simultaneous rows the extra ones simply fade: a dozen scatters
// at once is already more than the eye resolves, and 200 of them would blow any mesh
// budget however coarse it got.
export const SCATTER_MAX_ROWS = 12;
export const scatterGridFor = (count, index = 0) => {
  const n = Math.min(Math.max(1, count | 0), SCATTER_MAX_ROWS);
  if (index >= SCATTER_MAX_ROWS) return { cols: 0, rows: 0 };   // fade only, no dust
  const fine = { cols: CHAT_DISINTEGRATE_COLS, rows: CHAT_DISINTEGRATE_ROWS };
  const total = n * fine.cols * fine.rows;
  if (total <= SCATTER_TILE_BUDGET) return fine;
  // Scale both axes by the same factor, so the cells stay square-ish, and keep a floor
  // — below it the "dust" reads as broken glass instead.
  const k = Math.sqrt(SCATTER_TILE_BUDGET / total);
  return { cols: Math.max(8, Math.round(fine.cols * k)), rows: Math.max(4, Math.round(fine.rows * k)) };
};

// Play `el` out, then run `done`. `done` ALWAYS runs — with no element, under
// reduced motion, anywhere — because the removal must never depend on the animation.
export function leaveThenRemove(el, done = () => {}, { ms = LEAVE_MS, cols, rows } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  let reduced = false;
  try { reduced = matchMedia('(prefers-reduced-motion: reduce)').matches; } catch { /* assume motion is fine */ }
  if (!el?.classList || reduced) { finish(); return Promise.resolve(); }
  // Freeze the height so the collapse has something to animate from: `height: auto`
  // has no start value to transition away from.
  if (el.getBoundingClientRect) {
    const h = el.getBoundingClientRect().height;
    if (h) el.style.setProperty('--leave-h', `${h}px`);
  }
  // The row's own box collapses (the list closes the gap immediately) while a copy
  // scatters into particles in their OWN fixed layer with their own lifetime — the row
  // never waits for them. cols === 0 is the budget's "fade only" (scatterGridFor).
  if (cols !== 0) disintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}) });
  el.classList.add(LEAVING_CLASS);
  return new Promise((resolve) => setTimeout(() => { finish(); resolve(); }, ms));
}

// How long a wipe REALLY lasts on screen (browser motion.js twin): leaveThenRemove
// resolves on the short collapse while particles fall for DISINTEGRATE_MS, so a caller
// swapping in a placeholder must wait for the longer one. 0 under reduced motion.
export const wipeDurationMs = () => {
  let reduced = false;
  try { reduced = matchMedia('(prefers-reduced-motion: reduce)').matches; } catch { /* assume motion is fine */ }
  return reduced ? 0 : Math.max(LEAVE_MS, DISINTEGRATE_MS);
};

// ── List hold: wipes in flight (browser motion.js twin) ─────────────────────
// begin() opens one hold per playing leave/materialize and returns a settle fn: await
// it AFTER the removal — it waits out the REAL wipe (wipeDurationMs) and runs `settle`
// exactly once. While any hold is pending, out-of-band re-render triggers must wait —
// their rebuild cuts the leave short. finalizeAll() settles everything NOW, for a view
// closing mid-animation. Timers injectable, so the semantics are unit-tested.
export const createListHold = ({ settle = () => {}, wait = wipeDurationMs, setTimer = setTimeout } = {}) => {
  const pending = new Set();
  const begin = () => {
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      pending.delete(finish);
      settle();
    };
    pending.add(finish);
    return () => new Promise((resolve) => setTimer(() => { finish(); resolve(); }, wait()));
  };
  return {
    begin,
    finalizeAll: () => { for (const f of [...pending]) f(); },
    get holding() { return pending.size > 0; },
  };
};

// May a list's "nothing here" placeholder show RIGHT NOW? Only when it is truly empty
// AND no wipe is still playing — under a hold the empty state would land beneath the
// falling ash and read as appearing before the removal finished. Pure — unit-tested.
export const emptyStateVisible = (count, holding = false) => count === 0 && !holding;

// ── Disintegration ("the snap") ─────────────────────────────────────────────
// A removed element comes apart: cloned once per tile, each clone clipped to its own
// grid cell, the cells drifting off in a staggered sweep. Mirror of browser
// motion.js; the tiles live in a FIXED layer because the row is collapsing under them.
export const DISINTEGRATE_MS = 900;
// A fine grid: at 8x4 the cells read as big rectangles sliding apart. Small cells are
// what make it read as ash rather than a broken window. The cost is one clone per
// cell, so this is the practical ceiling for a list row.
export const DISINTEGRATE_COLS = 22;
export const DISINTEGRATE_ROWS = 11;

// Deterministic per-tile jitter — a hash, not Math.random, so the scatter is varied
// but reproducible (and unit-testable). Returns a 0..1 float.
export const tileNoise = (cx, cy) => {
  const h = Math.sin(cx * 127.1 + cy * 311.7) * 43758.5453;
  return h - Math.floor(h);
};

// The clip rectangle for one cell, as an inset() in percentages. Cells overlap by a
// hair so the grid shows no seams while it is still assembled.
export const tileInset = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS) => {
  const w = 100 / cols, h = 100 / rows;
  const top = cy * h, left = cx * w;
  const bleed = 0.4;
  return `inset(${Math.max(0, top - bleed)}% ${Math.max(0, 100 - left - w - bleed)}% `
    + `${Math.max(0, 100 - top - h - bleed)}% ${Math.max(0, left - bleed)}%)`;
};

// Where a cell goes and when it starts. The sweep erodes the element from one edge
// (delay grows with progress) and every cell drifts, further the later it goes.
// `reverse` inverts only the SWEEP (for the gather — see reintegrate): the flight
// path itself is shared, merely played backwards. Pure.
export const tileMotion = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS, reverse = false) => {
  const n = tileNoise(cx, cy);
  // A second, decorrelated noise so a mote's SIDEWAYS drift is independent of its fall
  // and its spin — one hash drove all three, which made whole diagonals move as one and
  // read as a sheet tearing rather than a thing coming apart. Browser motion.js twin.
  const m = tileNoise(cx + 41, cy + 17);
  // 0 at the TOP row (goes first), 1 at the bottom (goes last): the row crumbles from
  // its top edge downward, the way the cleared image does.
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  const delay = Math.round((reverse ? 1 - progress : progress) * DISINTEGRATE_MS * 0.4 + n * 60);
  // …and the motes FALL, fanning out as they go. Signed drift, so they spread both
  // ways instead of all sliding one.
  return {
    delay,
    dx: Math.round((m - 0.5) * 66),
    dy: Math.round(26 + progress * 30 + n * 44),
    rot: +((m - 0.5) * 70).toFixed(2),
    scale: +(0.3 + n * 0.3).toFixed(2),
  };
};

// Scatter `el`. Returns true when tiles were actually built; never throws — a failed
// scatter just means no particles. cloneNode is right for ordinary DOM but NOT for a
// <canvas> (a clone is blank), so callers pass a maker that blits the pixels instead.
export const cloneForTile = (el) => el.cloneNode(true);

// A canvas maker CANNOT hand back a full-size copy per cell — at this grid that would
// be hundreds of full-resolution bitmaps. Each cell gets only its own slice, drawn at
// cell size, and the tile positions it instead of clipping a full copy.
export const canvasCellForTile = (canvas, cell) => {
  const c = document.createElement('canvas');
  const sx = canvas.width / cell.cols, sy = canvas.height / cell.rows;
  c.width = Math.max(1, Math.ceil(sx));
  c.height = Math.max(1, Math.ceil(sy));
  c.getContext('2d').drawImage(canvas, cell.cx * sx, cell.cy * sy, sx, sy, 0, 0, c.width, c.height);
  return c;
};

export function disintegrate(el, { cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS,
                                   makeCopy = cloneForTile, perCell = false, gather = false } = {}) {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect || !document.body) return false;
  try {
    const r = el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    const host = document.createElement('div');
    host.className = 'disintegrate-host';
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    host.style.width = `${r.width}px`;
    host.style.height = `${r.height}px`;
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        const m = tileMotion(cx, cy, cols, rows, gather);
        const tile = document.createElement('div');
        // The gather class overrides the scatter's animation with stTileGather (the
        // same flight, played home) while inheriting the tile's box/clip rules.
        tile.className = gather ? 'disintegrate-tile reintegrate-tile' : 'disintegrate-tile';
        // A per-cell copy is already only its own slice, so it is POSITIONED; a full
        // clone covers the whole box and is CLIPPED down to its cell instead.
        if (!perCell) tile.style.clipPath = tileInset(cx, cy, cols, rows);
        tile.style.setProperty('--dx', `${m.dx}px`);
        tile.style.setProperty('--dy', `${m.dy}px`);
        tile.style.setProperty('--rot', `${m.rot}deg`);
        tile.style.setProperty('--tile-scale', String(m.scale));
        tile.style.animationDelay = `${m.delay}ms`;
        const copy = makeCopy(el, { cx, cy, cols, rows });
        copy.style.margin = '0';
        if (perCell) {
          copy.style.position = 'absolute';
          copy.style.left = `${(cx * r.width) / cols}px`;
          copy.style.top = `${(cy * r.height) / rows}px`;
          copy.style.width = `${r.width / cols}px`;
          copy.style.height = `${r.height / rows}px`;
        } else {
          // The clone is out of its parent's layout, so its box has to be restated.
          copy.style.width = `${r.width}px`;
          copy.style.height = `${r.height}px`;
          copy.classList.remove(LEAVING_CLASS, REVEAL_ITEM_CLASS, REVEAL_IN_CLASS);
        }
        tile.appendChild(copy);
        host.appendChild(tile);
      }
    }
    // Appended to the element's own PARENT, not <body>: most row styling is parent-
    // scoped, and a body-level clone matches none of it (tiles come out unstyled).
    // The host stays position:fixed, so it still escapes the scroller's clipping.
    (el.parentElement || document.body).appendChild(host);
    // …but only if the parent can actually host it: an ancestor with a transform /
    // filter / backdrop-filter becomes the containing block for position:fixed, which
    // re-anchors the layer AND lets overflow:hidden clip it away. Detected by MEASURING
    // — if the layer did not land where told, re-home it on <body> (unstyled but visible).
    const got = host.getBoundingClientRect();
    if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
      document.body.appendChild(host);
    }
    setTimeout(() => host.remove(), DISINTEGRATE_MS + 400);
    return true;
  } catch {
    return false;   // decoration only — the removal carries on regardless
  }
}

// ── Reintegration: the snap played backwards (browser motion.js twin) ───────
// The same tile layer as disintegrate, but every mote starts where the scatter would
// have flung it and flies HOME (stTileGather in animations.css), with the sweep
// reversed so the first mote out is the last one in. Used by materialize below.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });

// ── Materialize: leaveThenRemove reversed, for a freshly-ADDED row ──────────
// Call on the new row right after the render that inserted it: its box expands on the
// short timer while a dust copy gathers into its final rect over the full wipe; the
// row stays veiled until the motes land (the dust IS the row forming). Resolves once
// the veil lifts; decoration only — no element / reduced motion resolve immediately.
export const MATERIALIZE_CLASS = 'materializing';
export const MATERIALIZE_VEIL_CLASS = 'materialize-veil';
export function materialize(el, { ms = LEAVE_MS, cols, rows } = {}) {
  let reduced = false;
  try { reduced = matchMedia('(prefers-reduced-motion: reduce)').matches; } catch { /* assume motion is fine */ }
  if (!el?.classList || reduced) return Promise.resolve();
  // Freeze the natural height (the row is already laid out) so the expansion has
  // something to animate to — the same trick the leave plays with --leave-h.
  if (el.getBoundingClientRect) {
    const h = el.getBoundingClientRect().height;
    if (h) el.style.setProperty('--enter-h', `${h}px`);
  }
  // cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
  const dusted = cols !== 0 && reintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}) });
  el.classList.add(MATERIALIZE_CLASS);
  if (dusted) el.classList.add(MATERIALIZE_VEIL_CLASS);
  return new Promise((resolve) => setTimeout(() => {
    el.classList.remove(MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS);
    resolve();
  }, dusted ? wipeDurationMs() : ms));
}


// One-shot "it landed here" flash — restart-safe, so two drops in a row replay the
// animation instead of the second one silently doing nothing.
export function flashLanding(el, cls = 'just-dropped', ms = 900) {
  if (!el?.classList) return;
  clearTimeout(el._landingTimer);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts it
  el.classList.add(cls);
  el._landingTimer = setTimeout(() => el.classList.remove(cls), ms);
}
