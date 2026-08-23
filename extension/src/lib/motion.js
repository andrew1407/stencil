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

// ── Filtering a list, in and out ────────────────────────────────────────────
// A row the FILTER stopped admitting is not a row that was DELETED: no particles, and a
// shorter, lighter collapse than the destructive leave above — narrowing a list must
// never read as destroying part of it. Arrivals play the mirror of it.
export const FILTER_LEAVE_MS = 150;
export const FILTER_ENTER_MS = 180;
export const FILTER_OUT_CLASS = 'filter-out';
export const FILTER_IN_CLASS = 'filter-in';

// Does the user want motion at all? Never throws (no matchMedia outside a browser).
export const prefersReducedMotion = () => {
  try { return matchMedia('(prefers-reduced-motion: reduce)').matches; } catch { return false; }
};

// Which keys arrived and which went away between two renders, in render order. Pure.
export const diffListKeys = (prev = [], next = []) => {
  const had = new Set(prev);
  const has = new Set(next);
  return { entered: next.filter((k) => !had.has(k)), left: prev.filter((k) => !has.has(k)) };
};

// Play ONE row out as a filter exclusion (no particles, the light collapse), then run
// `done`. Like leaveThenRemove, `done` ALWAYS runs — no element or reduced motion just
// skips the animation. For a whole re-render use createFilterTransition below.
export function filterLeave(el, done = () => {}, { ms = FILTER_LEAVE_MS,
                                                   reduced = prefersReducedMotion, setTimer = setTimeout } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  if (!el?.classList || reduced()) { finish(); return Promise.resolve(); }
  if (el.getBoundingClientRect) {
    const h = el.getBoundingClientRect().height;
    if (h) el.style?.setProperty?.('--leave-h', `${h}px`);
  }
  el.classList.add(FILTER_OUT_CLASS);
  return new Promise((resolve) => setTimer(() => { finish(); resolve(); }, ms));
}

// Wrap a list that re-renders WHOLESALE (`innerHTML = ''` + rebuild) so a filter change
// animates both ways: call begin() before the wipe and end() after the rebuild. Rows
// whose key is new ramp in; rows whose key is gone are put back where they stood purely
// to play their exit ("ghosts") and dropped when it ends. Rows are matched by
// `el.dataset[keyAttr]`; anything without one (an empty-state row) is ignored.
//
// Correctness outranks the decoration: begin() kills every ghost still on screen first,
// so a burst of filter changes can neither stack animations nor strand a row — after
// end() the list holds exactly the rebuilt set, plus ghosts that are on their way out
// and belong to nothing. Under reduced motion nothing is added at all. Timers and the
// media query are injectable, so the semantics are unit-tested.
export const createFilterTransition = ({
  list, keyAttr = 'key', ms = FILTER_LEAVE_MS, enterMs = FILTER_ENTER_MS,
  reduced = prefersReducedMotion, setTimer = setTimeout, clearTimer = clearTimeout,
  onLeave = () => {},
} = {}) => {
  let ghosts = [];   // { el, timer } — on screen only to finish their exit
  let taken = [];    // begin()'s snapshot of the live rows
  const keyOf = (el) => (el && el.dataset ? el.dataset[keyAttr] : undefined);
  const kids = () => [...(list && list.children ? list.children : [])];

  const drop = (g) => {
    clearTimer(g.timer);
    g.el.remove?.();
    ghosts = ghosts.filter((x) => x !== g);
  };
  // Every ghost goes NOW: before each render (the same row must never animate twice)
  // and on teardown.
  const clear = () => {
    for (const g of [...ghosts]) drop(g);
    ghosts = [];
  };

  const begin = () => {
    clear();
    taken = kids().filter((el) => keyOf(el) != null).map((el) => ({ el, key: keyOf(el) }));
    return taken.map((t) => t.key);
  };

  // `skipEnter` = keys whose arrival the caller animates itself (a freshly added row
  // materializing), so the two effects don't stack on one element.
  const end = ({ skipEnter = [] } = {}) => {
    const before = taken;
    taken = [];
    const rows = kids();
    const diff = diffListKeys(before.map((t) => t.key), rows.map(keyOf).filter((k) => k != null));
    if (reduced()) return diff;   // straight to the final state, no classes, no ghosts
    const skip = new Set(skipEnter);
    const arriving = new Set(diff.entered.filter((k) => !skip.has(k)));
    for (const el of rows) {
      if (!arriving.has(keyOf(el)) || !el.classList) continue;
      el.classList.add(FILTER_IN_CLASS);
      setTimer(() => el.classList.remove(FILTER_IN_CLASS), enterMs + 40);
    }
    const gone = new Set(diff.left);
    before.forEach(({ el, key }, i) => {
      if (!gone.has(key) || !el.classList) return;
      onLeave(el);
      // Freeze the height so the collapse has a start value — `height: auto` has none
      // (the same trick leaveThenRemove plays with --leave-h).
      const h = el.getBoundingClientRect ? el.getBoundingClientRect().height : 0;
      if (h) el.style?.setProperty?.('--leave-h', `${h}px`);
      el.classList.remove(FILTER_IN_CLASS);
      el.classList.add(FILTER_OUT_CLASS);
      const at = (list.children && list.children[i]) || null;   // back where it stood
      if (typeof list.insertBefore === 'function') list.insertBefore(el, at);
      else list.appendChild(el);
      const g = { el, timer: null };
      g.timer = setTimer(() => drop(g), ms + 40);
      ghosts.push(g);
    });
    return diff;
  };

  return { begin, end, clear, get ghostCount() { return ghosts.length; } };
};

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

// Motes sized in PIXELS, not as a share of the element — a fixed grid over a wide row
// gives slivers, over a small card gives real dust. Aim for MOTE_PX; the quoted grid's
// cell COUNT is the frame-budget ceiling. Pure — unit-tested (browser motion.js twin).
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

// Drop the dust layer an element still owns, if any. A superseding open/close calls
// this, so a double-clicked menu never strands a cloud over the page.
export function cancelDust(el) {
  if (!el) return;
  clearTimeout(el.__dustTimer);
  el.__dustTimer = null;
  el.__dustHost?.remove?.();
  el.__dustHost = null;
}

export function disintegrate(el, { cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS,
                                   makeCopy = cloneForTile, perCell = false, gather = false,
                                   toward = null, ms = 0, px = MOTE_PX, spread = SURFACE_SPREAD,
                                   toBody = false, hostClass = '' } = {}) {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect || !document.body) return false;
  try {
    cancelDust(el);   // one cloud per element: the newest gesture owns it
    const r = el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    ({ cols, rows } = reshapeGrid(cols, rows, r.width, r.height, px));
    const host = document.createElement('div');
    host.className = hostClass ? `disintegrate-host ${hostClass}` : 'disintegrate-host';
    // Decoration, and nothing but: the layer must never take a click or a Tab stop —
    // a cloned menu is full of real <button>s.
    host.setAttribute('aria-hidden', 'true');
    host.inert = true;
    // A surface flies on its own (shorter) clock; a row keeps the CSS defaults.
    if (ms) {
      host.style.setProperty('--dust-ms', `${ms}ms`);
      host.style.setProperty('--gather-ms', `${ms}ms`);
    }
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    host.style.width = `${r.width}px`;
    host.style.height = `${r.height}px`;
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        // A row FALLS (tileMotion); a surface flies at the control that owns it.
        const m = toward
          ? surfaceMotion(cx, cy, cols, rows, r, toward, { span: ms || DISINTEGRATE_MS, spread })
          : tileMotion(cx, cy, cols, rows, gather);
        const tile = document.createElement('div');
        // The gather class overrides the scatter's animation with stTileGather (the
        // same flight, played home) while inheriting the tile's box/clip rules.
        tile.className = gather ? 'disintegrate-tile reintegrate-tile' : 'disintegrate-tile';
        // A per-cell copy is already only its own slice, so it is POSITIONED; a full
        // clone covers the whole box and is CLIPPED down to its cell instead.
        if (!perCell) tile.style.clipPath = tileInset(cx, cy, cols, rows);
        // Every tile is a FULL-SIZE box clipped to its cell, so the default 50% 50% origin
        // is the element's centre — scaling would shrink them all toward the middle, which
        // reads as imploding rather than coming apart. Each mote turns about its own cell.
        tile.style.transformOrigin = `${((cx + 0.5) / cols) * 100}% ${((cy + 0.5) / rows) * 100}%`;
        tile.style.setProperty('--dx', `${m.dx}px`);
        tile.style.setProperty('--dy', `${m.dy}px`);
        tile.style.setProperty('--rot', `${m.rot}deg`);
        tile.style.setProperty('--tile-scale', String(m.scale));
        tile.style.animationDelay = `${m.delay}ms`;
        const cellW = r.width / cols;
        const cellH = r.height / rows;
        const copy = makeCopy(el, { cx, cy, cols, rows, cellW, cellH });
        if (perCell) {
          copy.style.position = 'absolute';
          copy.style.left = `${cx * cellW}px`;
          copy.style.top = `${cy * cellH}px`;
          // A speck sizes ITSELF (it is a grain inside its cell, not the cell); a real
          // per-cell slice is drawn at cell size and needs the box stated.
          if (!copy.classList.contains('dust-mote')) {
            copy.style.margin = '0';
            copy.style.width = `${cellW}px`;
            copy.style.height = `${cellH}px`;
          }
        } else {
          copy.style.margin = '0';
          // The clone is out of its parent's layout, so its box has to be restated.
          copy.style.width = `${r.width}px`;
          copy.style.height = `${r.height}px`;
          copy.classList.remove(LEAVING_CLASS, REVEAL_ITEM_CLASS, REVEAL_IN_CLASS);
          // A clone REPLAYS its element's CSS entrance animation from t=0 — inside a tile
          // that reads as the thing flashing back before the scatter. The tile owns all
          // motion; the copy holds still.
          copy.style.animation = 'none';
        }
        tile.appendChild(copy);
        host.appendChild(tile);
      }
    }
    // Appended to the element's own PARENT, not <body>: most row styling is parent-
    // scoped, and a body-level clone matches none of it (tiles come out unstyled).
    // The host stays position:fixed, so it still escapes the scroller's clipping.
    // A SURFACE goes on <body> outright: its own parent (a dialog backdrop) is about to
    // be removed under it, and body is last in tree order, so a clone carrying duplicate
    // ids can never shadow the real thing in getElementById.
    (toBody ? document.body : (el.parentElement || document.body)).appendChild(host);
    // …but only if the parent can actually host it: an ancestor with a transform /
    // filter / backdrop-filter becomes the containing block for position:fixed, which
    // re-anchors the layer AND lets overflow:hidden clip it away. Detected by MEASURING
    // — if the layer did not land where told, re-home it on <body> (unstyled but visible).
    const got = host.getBoundingClientRect();
    if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
      document.body.appendChild(host);
    }
    el.__dustHost = host;
    el.__dustTimer = setTimeout(() => {
      host.remove();
      if (el.__dustHost === host) { el.__dustHost = null; el.__dustTimer = null; }
    }, (ms || DISINTEGRATE_MS) + 400);
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


// ── Surfaces: a menu, a dialog and a mini popup are dust too ────────────────
// Browser motion.js twin. A modal and the ⋯/context/dropdown popups play the SAME
// scatter a deleted row does — only every mote flies INTO (or out of) the point that
// owns the surface: the icon that opened it, or the click a context menu grew from.
// Origin and direction are exactly what the old scale had; what changed is that the
// flight is rendered as particles instead of a moving rectangle.
// The way IN is the slower half on purpose: a surface forming is the thing you watch,
// and it has to arrive gently enough to read as sand gathering rather than a flash.
// Going out is brisk — you have already decided.
export const SURFACE_IN_MS = 620;
export const SURFACE_OUT_MS = 380;
// A MENU is not a window: it is opened to be clicked, often blind, so it may not spend
// half a second forming. Its own, brisker clock — the flight is the same one.
export const SURFACE_MENU_IN_MS = 340;
export const SURFACE_MENU_OUT_MS = 220;
// The grain a mote AIMS for, and the ceiling on how many of them a flight may cost.
// A window is tens of times a row's area, so the budget is what actually sizes its
// cells: at 1200 an options dialog came apart into 20px slabs — a mosaic, not sand.
export const SURFACE_MOTE_PX = 6;
export const SURFACE_COLS = 60;
export const SURFACE_ROWS = 40;      // 2400 motes
// …and past that ceiling the CELL is bigger than the grain we want, so the speck drawn
// inside it is capped instead of filling it. What you see is the speck, not the cell.
export const SURFACE_SPECK_PX = 7;
export const SURFACE_SPREAD = 34;    // how far a mote may fan off its line to the point
export const SURFACE_FORMING_CLASS = 'surface-forming';
export const SURFACE_LEAVING_CLASS = 'surface-leaving';
// Permanent once a surface has been dusted: its old CSS pop must stay off for good, or
// removing the forming class at the end of the flight would replay it.
export const SURFACE_DRIVEN_CLASS = 'dust-driven';

// One mote's flight when a whole surface gathers into — or bursts out of — a single
// POINT. The path is the cell's own offset to that point, so every mote converges there
// instead of falling; the two decorrelated noises only fan the arrival. The delay rides
// the DISTANCE, so the edge nearest the point goes first and the far one last — a menu
// draining into its icon, and pouring back out of it. Pure — unit-tested.
export const surfaceMotion = (cx, cy, cols, rows, box, point, { span = SURFACE_OUT_MS, spread = SURFACE_SPREAD } = {}) => {
  const n = tileNoise(cx, cy);
  const m = tileNoise(cx + 41, cy + 17);
  const w = (box?.width || 0) / Math.max(1, cols);
  const h = (box?.height || 0) / Math.max(1, rows);
  const mx = (box?.left || 0) + (cx + 0.5) * w;
  const my = (box?.top || 0) + (cy + 0.5) * h;
  const toX = (point?.x || 0) - mx;
  const toY = (point?.y || 0) - my;
  // Normalised against the longest trip any cell in this box makes, so the sweep fills
  // the whole flight whatever the point's distance is.
  const far = Math.hypot(box?.width || 0, box?.height || 0) + Math.hypot(toX, toY);
  const progress = far > 0 ? Math.min(1, Math.hypot(toX, toY) / far) : 0;
  return {
    delay: Math.round(progress * span * 0.45 + n * span * 0.12),
    dx: Math.round(toX + (m - 0.5) * spread),
    dy: Math.round(toY + (n - 0.5) * spread),
    rot: +((m - 0.5) * 60).toFixed(2),
    scale: +(0.12 + n * 0.25).toFixed(2),
  };
};

// The centre of the control a surface was opened from — the point its motes fly out of
// and back into. Null (no rect) leaves the caller to pick one. Pure.
export const centerOf = (elOrRect) => {
  const r = elOrRect?.getBoundingClientRect ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !Number.isFinite(r.left) || !Number.isFinite(r.top)) return null;
  return { x: r.left + (r.width || 0) / 2, y: r.top + (r.height || 0) / 2 };
};

// May this surface afford a REAL clone per cell? That is what makes a menu come apart
// into pieces of itself, and a small menu is cheap enough for it. An options dialog is
// hundreds of nodes and the grid is hundreds of cells, and the PRODUCT is what would
// stall the frame — past this budget the motes are flat specks in the surface's own
// colours, which at mote size is nearly all a clone would show anyway. Pure.
export const SURFACE_CLONE_NODE_BUDGET = 12000;
export const surfaceClones = (nodes, tiles) =>
  Math.max(1, nodes || 0) * Math.max(0, tiles || 0) <= SURFACE_CLONE_NODE_BUDGET;

// Is `c` a colour that paints nothing? An unset background, or a fully transparent one.
const blankPaint = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);

// What the motes are PAINTED in. The element's own background, else the nearest
// ancestor that actually paints one — a surface whose box is transparent (a panel that
// leaves the colour to a child) would otherwise dust in a fallback nobody chose.
const surfacePaint = (el) => {
  const get = typeof getComputedStyle === 'function' ? getComputedStyle : null;
  const own = get ? get(el) : null;
  let fill = '';
  for (let node = el; get && node && node.nodeType === 1 && !fill; node = node.parentElement)
    if (!blankPaint(get(node).backgroundColor)) fill = get(node).backgroundColor;
  // An element with `border-style: none` still COMPUTES a border colour, and its
  // initial value is `currentColor` — the TEXT colour. Reading it unguarded painted
  // every rim mote near-white on a dark theme, whatever the theme actually was.
  const bordered = own && own.borderTopStyle !== 'none' && parseFloat(own.borderTopWidth) > 0;
  return { fill: fill || 'var(--panel)', edge: bordered ? own.borderTopColor : '' };
};

// Flat speck in the surface's own colours; the rim cells take its border instead, so the
// cloud keeps the box's outline for the first frames. The speck is a GRAIN, not the cell
// it sits in: past the mote budget a cell can be several times the grain we want, and a
// cell-filling square is the "huge rectangles" a scatter must never show.
const speckMaker = (el) => {
  const { fill, edge } = surfacePaint(el);
  return (_el, { cx, cy, cols, rows, cellW, cellH }) => {
    const d = document.createElement('div');
    d.className = 'dust-mote';
    const n = tileNoise(cx, cy);
    d.style.background = (edge && (cx === 0 || cy === 0 || cx === cols - 1 || cy === rows - 1))
      ? edge : fill;
    d.style.opacity = (0.62 + n * 0.38).toFixed(2);
    // Grains of ONE size read as a mosaic; the spread is what makes it sand.
    const grain = Math.min(cellW || SURFACE_SPECK_PX, cellH || SURFACE_SPECK_PX, SURFACE_SPECK_PX);
    const px = grain * (0.62 + n * 0.5);
    d.style.width = `${px.toFixed(2)}px`;
    d.style.height = `${px.toFixed(2)}px`;
    // …centred in its cell, so the field stays even however far the cell outgrew it.
    d.style.marginLeft = `${(((cellW || px) - px) / 2).toFixed(2)}px`;
    d.style.marginTop = `${(((cellH || px) - px) / 2).toFixed(2)}px`;
    return d;
  };
};

// A menu is position:fixed. Inside a tile it must lay out from the tile's own corner
// instead of re-anchoring to the viewport — or, once the tile is moving, to the tile.
const surfaceCloneMaker = () => (el) => {
  const c = cloneForTile(el);
  if (c.style) { c.style.position = 'static'; c.style.left = ''; c.style.top = ''; }
  return c;
};

const surfaceDust = (el, point, { ms, gather }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
  const r = el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, r.width, r.height, SURFACE_MOTE_PX);
  const nodes = el.getElementsByTagName ? el.getElementsByTagName('*').length : 0;
  const clone = surfaceClones(nodes, grid.cols * grid.rows);
  return disintegrate(el, {
    ...grid, gather, toward: point, ms, px: SURFACE_MOTE_PX, toBody: true,
    hostClass: gather ? 'dust-forming' : 'dust-leaving',
    perCell: !clone, makeCopy: clone ? surfaceCloneMaker() : speckMaker(el),
  });
};

// Drop whatever a surface has in flight — the cloud AND the classes driving its own
// opacity — leaving the end state untouched. Every open/close begins here, so a
// double-clicked menu or a swept-past dialog always converges on the true state.
export function settleSurface(el) {
  if (!el?.classList) return;
  clearTimeout(el.__surfaceTimer);
  el.__surfaceTimer = null;
  el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
  el.style?.removeProperty?.('--dust-ms');
  cancelDust(el);
}

const playSurface = (el, point, { ms, gather }) => {
  if (!el?.classList) return false;
  settleSurface(el);
  if (prefersReducedMotion()) return false;
  // The marker goes on BEFORE the measure. The element's own entrance (action-menu-pop,
  // stMenuFromAnchor) fills its from-state — an icon-sized scale — so a box measured
  // under it is the ICON's box, and every mote would be built from a 30px menu.
  // `.dust-driven` kills that keyframe outright. Off again if the dust declines, so a
  // surface that never plays it keeps the CSS entrance it always had.
  el.classList.add(SURFACE_DRIVEN_CLASS);
  if (!surfaceDust(el, point, { ms, gather })) {
    el.classList.remove(SURFACE_DRIVEN_CLASS);
    return false;
  }
  el.style?.setProperty?.('--dust-ms', `${ms}ms`);
  el.classList.add(gather ? SURFACE_FORMING_CLASS : SURFACE_LEAVING_CLASS);
  el.__surfaceTimer = setTimeout(() => {
    el.__surfaceTimer = null;
    el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
    el.style?.removeProperty?.('--dust-ms');
  }, ms + 60);
  return true;
};

// The surface waits behind its own dust and fades up as the last motes land.
export const surfaceIn = (el, point, { ms = SURFACE_IN_MS } = {}) =>
  playSurface(el, point, { ms, gather: true });
// …and hands over to it at once on the way out. The caller still owns the real
// hide/remove: like leaveThenRemove, the end state never depends on the animation.
export const surfaceOut = (el, point, { ms = SURFACE_OUT_MS } = {}) =>
  playSurface(el, point, { ms, gather: false });
// The browser's spelling of the same check, so the modules ported verbatim from
// browser/js/ui (dropdownMenu, controlTooltip — extension/tests/portParity.test.js)
// can name it exactly as they do there.
export const motionReduced = prefersReducedMotion;

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
