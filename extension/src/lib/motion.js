// ── Shared UI motion helpers (mirror of browser/js/ui/motion.js) ────────────
// Pure decoration: with no IntersectionObserver/MutationObserver nothing runs and lists
// show normally. CSS owns the keyframes (lib/animations.css); this only toggles classes.
import { startCloud, resolveColour, PARTICLE_STYLES, paletteCss } from './dustCloud.js';

// Rows dissolve only by the amount the scroller is ALREADY clipping them: decoration
// must never cost legibility (the grain is finer than a glyph's strokes).
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
// do — a row taller than the scroller would stay speckled. Showing as much as the
// viewport holds counts as fully visible. Pure.
export const revealGrain = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / Math.min(h, viewH);
};

// Watch `root` and ramp every child matching `selector` as it scrolls. Returns a
// disconnect function; safe in any environment. Scrolling must stay cheap: geometry is
// measured once per list change, never per frame (rect reads force a layout).
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
// A row about to be destroyed collapses and fades first, so a delete reads as the row
// going away rather than the list jumping. The caller still does the removal in the
// callback — this only buys it the time. Mirrors the browser twin.
export const LEAVE_MS = 220;
// Chat entries leave more slowly and in a finer grid than a list row (browser twin):
// the extra time reads as "it dissolved" rather than "it blinked out".
export const CHAT_LEAVE_MS = 260;
export const CHAT_DISINTEGRATE_COLS = 32;
export const CHAT_DISINTEGRATE_ROWS = 16;
export const LEAVING_CLASS = 'leaving';

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

// Play `el` out, then run `done`. `done` ALWAYS runs — with no element, under
// reduced motion, anywhere — because the removal must never depend on the animation.
export function leaveThenRemove(el, done = () => {}, { ms = LEAVE_MS, cols, rows } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  if (!el?.classList || motionReduced()) { finish(); return Promise.resolve(); }
  // Freeze the height so the collapse has something to animate from: `height: auto`
  // has no start value to transition away from.
  if (el.getBoundingClientRect) {
    const h = el.getBoundingClientRect().height;
    if (h) el.style.setProperty('--leave-h', `${h}px`);
  }
  // The box collapses (the list closes the gap at once) while a copy scatters in its OWN
  // fixed layer — the row never waits for it. cols === 0 is scatterGridFor's "fade only".
  if (cols !== 0) disintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}) });
  el.classList.add(LEAVING_CLASS);
  return new Promise((resolve) => setTimeout(() => { finish(); resolve(); }, ms));
}

// How long a wipe REALLY lasts on screen (browser motion.js twin): leaveThenRemove
// resolves on the short collapse while particles fall for DISINTEGRATE_MS, so a caller
// swapping in a placeholder must wait for the longer one.
export const wipeDurationMs = () => {
  if (motionReduced()) return 0;
  return dustEnabled() ? Math.max(LEAVE_MS, DISINTEGRATE_MS) : LEAVE_MS;
};

// ── List hold: wipes in flight (browser motion.js twin) ─────────────────────
// begin() opens one hold per playing leave/materialize and returns a settle fn: await it
// AFTER the removal — it waits out the REAL wipe (wipeDurationMs) and runs `settle` once.
// While a hold is pending an out-of-band re-render must wait; its rebuild cuts the leave
// short. finalizeAll() settles everything NOW, for a view closing mid-animation.
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
// lighter collapse — narrowing a list must never read as destroying part of it.
export const FILTER_LEAVE_MS = 150;
export const FILTER_ENTER_MS = 180;
export const FILTER_OUT_CLASS = 'filter-out';
export const FILTER_IN_CLASS = 'filter-in';

// Does the OS want motion at all? Never throws (no matchMedia outside a browser).
export const prefersReducedMotion = () => {
  try { return matchMedia('(prefers-reduced-motion: reduce)').matches; } catch { return false; }
};
// ── The motion mode (browser motionPrefs.js twin) ───────────────────────────
// The user's switch, kept by the pre-paint classic script lib/accent.js
// (window.StencilMotion), asked live so an options-page change reaches an open popup
// without a reload. Without the script only the OS preference speaks.
const motionPref = () => globalThis.StencilMotion || null;
export const motionMode = () => motionPref()?.get?.() ?? 'particles';
// The one gate every animation checks: nothing may move.
export const motionReduced = () => (motionPref() ? motionPref().reduced() : prefersReducedMotion());
// …and the one every PARTICLE flight checks on top of it. False in 'slide' leaves the
// surface's own CSS entrance in charge — the flight the particles normally replace.
export const dustEnabled = () => (motionPref() ? motionPref().particles() : !prefersReducedMotion());
// Which style the particles wear — 'dust' | 'water' | 'fire' — or null when none fly.
export const particleStyle = () => (motionPref() ? motionPref().style() : (prefersReducedMotion() ? null : 'dust'));
// …as dustCloud.js's code (0 = dust, the flight as tabulated).
const styleCode = () => PARTICLE_STYLES[particleStyle()] || 0;

// Which keys arrived and which went away between two renders, in render order. Pure.
export const diffListKeys = (prev = [], next = []) => {
  const had = new Set(prev);
  const has = new Set(next);
  return { entered: next.filter((k) => !had.has(k)), left: prev.filter((k) => !has.has(k)) };
};

// Play ONE row out as a filter exclusion (no particles, the light collapse), then run
// `done` — which ALWAYS runs, as in leaveThenRemove. For a whole re-render use
// createFilterTransition below.
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
// animates both ways: begin() before the wipe, end() after the rebuild. New keys ramp in;
// gone keys are put back where they stood purely to play their exit ("ghosts"). Matched by
// `el.dataset[keyAttr]`; anything without one (an empty-state row) is ignored.
// Correctness outranks the decoration: begin() kills every ghost first, so a burst of
// filter changes can neither stack animations nor strand a row.
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
  // Every ghost goes NOW: the same row must never animate twice.
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
      // Freeze the height so the collapse has a start value — `height: auto` has none.
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
// A removed element comes apart into MOTES: one round speck per grid cell in the
// element's own colours (speckPainter), drifting off in a staggered sweep. Never clones
// of the element — hundreds of copies of a row's subtree show nothing a speck does not.
// Mirror of browser motion.js; the layer is FIXED because the row collapses under it.
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
// (--mx/--my; animations.css stTileScatter and kin). Pure — unit-tested.
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
const flightOf = (toward, gather) =>
  toward ? (gather ? 'surfaceGather' : 'surfaceScatter') : (gather ? 'gather' : 'scatter');

export function disintegrate(el, { cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS,
                                   gather = false, toward = null, ms = 0, px = MOTE_PX,
                                   spread = SURFACE_SPREAD, toBody = false, hostEl = null,
                                   hostClass = '', paintTile = null } = {}) {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect || !document.body) return false;
  // Every element-sized cloud is built here, so this is where the mode turns particles
  // off: a `false` return leaves the caller on its own CSS entrance.
  if (!dustEnabled()) return false;
  try {
    cancelDust(el);   // one cloud per element: the newest gesture owns it
    const r = el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    ({ cols, rows } = reshapeGrid(cols, rows, r.width, r.height, px));
    // Specks in the element's own colours unless the caller brought a recipe.
    const paint = paintTile || speckPainter(el);
    const host = document.createElement('div');
    host.className = hostClass ? `disintegrate-host ${hostClass}` : 'disintegrate-host';
    // Decoration, and nothing but: the layer must never take a click or a Tab stop.
    host.setAttribute('aria-hidden', 'true');
    host.inert = true;
    const span = ms || DISINTEGRATE_MS;
    // A surface flies on its own (shorter) clock; a row keeps the defaults. A ROW gather
    // on its own clock keeps the default's proportions, so the last mote to set off still
    // lands before the veil lifts.
    const gatherMs = toward || !gather ? span : Math.round(span * TILE_GATHER_SHARE);
    if (ms) {
      host.style.setProperty('--dust-ms', `${ms}ms`);
      host.style.setProperty('--gather-ms', `${gatherMs}ms`);
    }
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    host.style.width = `${r.width}px`;
    host.style.height = `${r.height}px`;
    const cellW = r.width / cols;
    const cellH = r.height / rows;
    // Every grain computed once: home, throw and bend (tileMotion / surfaceMotion),
    // colour, size, clock. ONE canvas then evaluates these per frame (dustCloud.js) — no
    // node per mote, so a dialog-sized cloud costs batched fills, not hundreds of layers.
    const motes = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        // A row FALLS (tileMotion); a surface flies at the control that owns it.
        const m = toward
          ? surfaceMotion(cx, cy, cols, rows, r, toward, { span, spread })
          : tileMotion(cx, cy, cols, rows, gather, span);
        // The speck's size, opacity and glint; its colour comes from the palette below.
        const speck = paint({ cx, cy, cols, rows, cellW, cellH });
        // The sweep is INSIDE the span, never added to it: a late mote flies the window
        // it has left, so the whole cloud is done at `span` with no stragglers.
        motes.push({
          x: r.left + (cx + 0.5) * cellW, y: r.top + (cy + 0.5) * cellH,
          dx: m.dx, dy: m.dy, mx: m.mx, my: m.my, r: speck.px / 2, s: m.scale, a: speck.alpha,
          delay: m.delay, dur: gather ? gatherMs : Math.max(MIN_TILE_MS, span - m.delay),
          // …and its own hash for the wobble, the twinkle and its place in the palette
          // (dustCloud.js turbulenceAt / dustMix).
          w: tileNoise(cx + 13, cy + 71), t: 1, g: speck.glint ? 1 : 0,
        });
      }
    }
    const kind = flightOf(toward, gather);
    // Every cloud is painted from the theme's palette, never in the surface's own colours:
    // each grain picks its stop by its mix and its tint (dustCloud.js stopOfTint).
    const style = styleCode();
    const paints = paletteCss();
    host.__cloud = { motes, colours: paints, flight: kind, span, style };   // what a test reads
    // The element's own PARENT, not <body>: a row's cloud is torn down with its list, and
    // position:fixed still escapes the scroller's clipping. A SURFACE goes on <body> —
    // its parent (a dialog backdrop) is about to be removed under it. `hostEl` is the chat
    // entry's middle ground: outside the transcript nothing that walks it meets the layer.
    (toBody ? document.body : (hostEl || el.parentElement || document.body)).appendChild(host);
    // …but only if that parent can host it: an ancestor with a transform/filter becomes
    // the containing block for position:fixed, re-anchoring the layer and letting
    // overflow:hidden clip it. Detected by MEASURING; if it missed, re-home on <body>.
    const got = host.getBoundingClientRect();
    if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
      document.body.appendChild(host);
    }
    // Colours resolved ONCE per cloud, after the host is in the document — a `var(--…)`
    // needs the page's own scope to mean anything.
    const probe = document.createElement('span');
    host.appendChild(probe);
    const fills = paints.map((css) => resolveColour(document, css, probe));
    probe.remove();
    startCloud(host, motes, { flight: kind, span, colours: fills, origin: { x: r.left, y: r.top }, style });
    el.__dustHost = host;
    el.__dustTimer = setTimeout(() => {
      host.__stop?.();
      host.remove();
      if (el.__dustHost === host) { el.__dustHost = null; el.__dustTimer = null; }
    }, span + 400);
    return true;
  } catch {
    return false;   // decoration only — the removal carries on regardless
  }
}

// ── Reintegration: the snap played backwards (browser motion.js twin) ───────
// Every mote starts where the scatter would have flung it and flies HOME (stTileGather in
// animations.css), sweep reversed so the first mote out is the last one in.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });

// ── Materialize: leaveThenRemove reversed, for a freshly-ADDED row ──────────
// Call right after the render that inserted the row: its box expands on the short timer
// while a dust copy gathers into its final rect, and it stays veiled until the motes land
// (the dust IS the row forming). Decoration only — reduced motion resolves at once.
export const MATERIALIZE_CLASS = 'materializing';
export const MATERIALIZE_VEIL_CLASS = 'materialize-veil';
export function materialize(el, { ms = LEAVE_MS, cols, rows } = {}) {
  if (!el?.classList || motionReduced()) return Promise.resolve();
  // Freeze the natural height so the expansion has something to animate to.
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

// ── A chat entry ARRIVES as dust (the mirror of leaveThenRemove) ────────────
// Browser motion.js twin: the same mesh and flight, flown HOME (reintegrate). The entry is
// HELD BACK for the whole flight — fading it up underneath shows the message before its
// own animation. On a clock well short of a row's: the motes carry no text, so a long
// answer is unreadable until the veil lifts. A FRACTION, so shortening the span shortens
// this with it.
export const CHAT_ENTER_MS = Math.round(DISINTEGRATE_MS * 0.58);
export const CHAT_ENTERING_CLASS = 'chat-entering';

// Two frames, so the measure below happens on a SETTLED transcript: frame one is the
// entry's layout, frame two the scroll that follows it. No rAF (node) ⇒ a macrotask.
const afterLayout = (fn) => (typeof requestAnimationFrame === 'function'
  ? requestAnimationFrame(() => requestAnimationFrame(fn))
  : setTimeout(fn, 0));

// Is `el` a whole entry sitting inside its scroller right now? The cloud is
// position:fixed, so the transcript does NOT clip it: an entry below the fold — or taller
// than the scroller — would scatter motes over the composer. Shared with trackDust.
const rectInScroller = (r, s) => !!(r && s && r.width > 0 && r.height > 0
  && r.top >= s.top - 1 && r.bottom <= s.bottom + 1);

export const dustFitsScroller = (el, scroller = el?.parentElement) => {
  if (!el?.getBoundingClientRect || !scroller?.getBoundingClientRect) return false;
  return rectInScroller(el.getBoundingClientRect(), scroller.getBoundingClientRect());
};

// Confine a flying cloud to its SCROLLER: the tiles translate freely out of an
// `overflow: visible` host, so a gather next to the input rains motes across it. The clip
// is against the host's own border box — negative insets EXPAND it, so a mote may fly
// anywhere inside the transcript. Re-applied per frame by trackDust: both boxes move.
const dustClipInset = (r, s) => {
  const px = (n) => `${Math.round(n)}px`;
  return `inset(${px(s.top - r.top)} ${px(r.right - s.right)} ${px(r.bottom - s.bottom)} ${px(s.left - r.left)})`;
};
const clipDustToScroller = (el, scroller = el?.parentElement) => {
  const host = el?.__dustHost;
  if (!host || !scroller?.getBoundingClientRect || !el.getBoundingClientRect) return;
  host.style.clipPath = dustClipInset(el.getBoundingClientRect(), scroller.getBoundingClientRect());
};

// Keep a flying cloud pinned to its entry until the motes land. The layer is
// position:fixed at its launch box, but the transcript SCROLLS under it, so a cloud left
// there is drawn over whatever has since moved into those coordinates. Re-anchored per
// frame; an entry that leaves the scroller drops its cloud. Returns a stop function.
const trackDust = (el, ms, onDrop = () => {}) => {
  if (typeof requestAnimationFrame !== 'function') return () => {};
  let raf = 0;
  let live = true;
  const started = Date.now();
  // The box the cloud was photographed at. A subject that RESIZES mid-flight (a rewrap, a
  // font landing, a drag) leaves a cloud that no longer matches what arrives, and there is
  // no re-photographing it: the stale copy is dropped. Missing beats lying.
  const shot = el.getBoundingClientRect?.();
  let last = {};   // the anchor/clip already written — an unchanged frame writes nothing
  const step = () => {
    if (!live) return;
    if (Date.now() - started >= ms) return;
    // Each box measured ONCE per frame: reads first, writes batched below, or every
    // flying cloud costs a forced layout per frame.
    const r = el.getBoundingClientRect?.();
    const s = el.parentElement?.getBoundingClientRect?.();
    const resized = !r || !shot || Math.abs(r.width - shot.width) > 1 || Math.abs(r.height - shot.height) > 1;
    // Dropping the cloud must HAND THE ENTRY OVER in the same frame: the veil lifts on a
    // timer at the end of the flight, so killing only the motes leaves nothing on screen.
    if (!rectInScroller(r, s) || resized) { cancelDust(el); live = false; onDrop(); return; }
    // Re-anchor + re-clip (retargetDust/clipDustToScroller), reads done, writes batched.
    const host = el.__dustHost;
    if (host) {
      const next = { left: `${r.left}px`, top: `${r.top}px`, clip: dustClipInset(r, s) };
      if (next.left !== last.left) host.style.left = next.left;
      if (next.top !== last.top) host.style.top = next.top;
      if (next.clip !== last.clip) host.style.clipPath = next.clip;
      last = next;
    }
    raf = requestAnimationFrame(step);
  };
  raf = requestAnimationFrame(step);
  return () => { live = false; if (raf) cancelAnimationFrame(raf); };
};

// A chat entry's only entrance is its dust, so in 'slide' it gets the rise the others
// already have (browser css/animations/motionModes.css chatRiseIn twin).
export const CHAT_SLIDE_CLASS = 'chat-slide-in';
export const CHAT_SLIDE_MS = 320;
export function chatIn(el, count = 1, index = 0, { host = null } = {}) {
  if (!el?.classList || motionReduced()) return Promise.resolve();
  if (!dustEnabled()) { flashLanding(el, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS); return Promise.resolve(); }
  const { cols, rows } = scatterGridFor(count, index);
  // Veiled from the FIRST frame: the entry keeps its height (so the transcript grows and
  // scrolls as usual) but is never seen ahead of its own motes.
  el.classList.add(CHAT_ENTERING_CLASS);
  const unveil = () => el.classList.remove(CHAT_ENTERING_CLASS);
  return new Promise((resolve) => {
    // Fonts first, when the platform offers the promise: a webfont landing after the
    // photograph re-wraps the entry, so the cloud is the wrong size for what arrives.
    // Loaded fonts resolve in the same tick, so only a session's first arrival waits.
    const ready = globalThis.document?.fonts?.ready;
    const go = () => afterLayout(() => {
      // `host` is the ancestor the bubble rules are scoped to (assistant.js passes its
      // section): outside the transcript, so nothing walking it sees `.msg` clones as live
      // conversation, but inside the ancestor those rules reach — on <body> the motes lose
      // their fill, border and radius. Browser parity: `.chat-msg` is styled standalone
      // there, so its cloud can sit on <body>. No host = the entry's own parent.
      const flying = cols !== 0 && dustFitsScroller(el)
        && reintegrate(el, { cols, rows, hostEl: host || el.parentElement || null, ms: CHAT_ENTER_MS });
      if (!flying) { unveil(); resolve(); return; }
      clipDustToScroller(el);   // before the first frame paints, not after it
      let handedOver = false;
      const handOver = () => {
        if (handedOver) return;   // the cut happens once, whichever path gets there first
        handedOver = true;
        unveil();
        cancelDust(el);
        resolve();
      };
      const stop = trackDust(el, CHAT_ENTER_MS, handOver);
      setTimeout(() => {
        // The entry takes their place in the SAME frame the cloud goes: left to its
        // grace period the layer holds its finished state, an exact second copy.
        stop();
        handOver();
      }, CHAT_ENTER_MS);
    });
    if (ready?.then) ready.then(go, go); else go();
  });
}


// ── Surfaces: a menu, a dialog and a mini popup are dust too ────────────────
// Browser motion.js twin. A modal and the ⋯/context/dropdown popups play the SAME scatter
// a deleted row does, only every mote flies into (or out of) the point that owns the
// surface. IN is the slower half on purpose — a surface forming is the thing you watch;
// going out is brisk, you have already decided.
export const SURFACE_IN_MS = 620;
export const SURFACE_OUT_MS = 380;
// A MENU is opened to be clicked, often blind, so it may not spend half a second forming.
// Its own, brisker clock — the flight is the same one.
export const SURFACE_MENU_IN_MS = 340;
export const SURFACE_MENU_OUT_MS = 220;
// A hover TIP is brisker still: re-triggered mid-sweep, its flight must be over before the
// next begins. Names shared with browser motion.js so the ported modules import them as-is.
export const TIP_DUST_IN_MS = 260;
export const TIP_DUST_OUT_MS = 190;
// …and wakes on one delay across surfaces (desktop SnappyTooltipStyle, main.cpp).
export const TIP_SHOW_DELAY_MS = 200;

// The centre of an element (or rect): the point a popup's dust belongs to. Null for a
// detached owner — the flight then settles instead.
export const rectCenter = (elOrRect) => {
  const r = typeof elOrRect?.getBoundingClientRect === 'function'
    ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !(r.width > 0 || r.height > 0)) return null;
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
};
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

// Is `c` a colour that paints nothing? An unset background, or a fully transparent one.
const blankPaint = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);

// How far a mote is lifted off the surface's background towards its ink — body, then rim.
export const MOTE_INK = 42;
export const MOTE_RIM_INK = 66;

// What the motes are PAINTED in: the element's own background, else the nearest ancestor
// that actually paints one (a transparent box would dust in a fallback nobody chose), then
// LIFTED towards that surface's ink — the background alone is the same family of colour as
// the page and dissolves into it. Mixing in the ink flips with the theme for free.
const surfacePaint = (el) => {
  const get = typeof getComputedStyle === 'function' ? getComputedStyle : null;
  const own = get ? get(el) : null;
  let bg = '';
  for (let node = el; get && node && node.nodeType === 1 && !bg; node = node.parentElement)
    if (!blankPaint(get(node).backgroundColor)) bg = get(node).backgroundColor;
  bg = bg || 'var(--panel)';
  const ink = own && !blankPaint(own.color) ? own.color : 'var(--text)';
  const grain = (pct) => `color-mix(in srgb, ${bg} ${pct}%, ${ink})`;
  // `border-style: none` still COMPUTES a border colour, and its initial value is
  // `currentColor` — the TEXT colour, which paints every rim mote near-white on a dark
  // theme. Without a real border the rim is simply a stronger grain.
  const bordered = own && own.borderTopStyle !== 'none' && parseFloat(own.borderTopWidth) > 0;
  return { fill: grain(100 - MOTE_INK), edge: bordered ? own.borderTopColor : grain(100 - MOTE_RIM_INK) };
};

// Round speck in the surface's own colours; rim cells take its border, so the cloud keeps
// the window's outline for the first frames, and a few inner grains take that tone too so
// the field glints. The speck is a GRAIN, not the cell it sits in: past the mote budget a
// cell-filling square is the "huge rectangles" a scatter must never show.
const speckPainter = (el) => {
  const { fill, edge } = surfacePaint(el);
  return ({ cx, cy, cols, rows, cellW, cellH }) => {
    const n = tileNoise(cx, cy);
    const rim = cx === 0 || cy === 0 || cx === cols - 1 || cy === rows - 1 || n > 0.86;
    // Grains of ONE size read as a mosaic; the spread is what makes it sand…
    const grain = Math.min(cellW, cellH, SURFACE_SPECK_PX);
    // …never faint: a mote you can barely see is a flight you cannot follow.
    return { color: rim ? edge : fill, alpha: 0.78 + n * 0.22, px: grain * (0.62 + n * 0.5), glint: rim };
  };
};

// A surface NEVER dusts as clones of itself: its cloud lands on <body>, so a few hundred
// copies of a menu is a few hundred more elements answering to `.action-menu` and kin, and
// everything that queries the page would have to know about a decoration. Flat specks
// carry no identity, and at a 6px grain show all a clone would.
const surfaceDust = (el, point, { ms, gather }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
  const r = el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, r.width, r.height, SURFACE_MOTE_PX);
  return disintegrate(el, {
    ...grid, gather, toward: point, ms, px: SURFACE_MOTE_PX, toBody: true,
    hostClass: gather ? 'dust-forming' : 'dust-leaving',
    paintTile: speckPainter(el),
  });
};

// Drop whatever a surface has in flight — the cloud AND the classes driving its opacity —
// leaving the end state untouched. Every open/close begins here, so a double-clicked menu
// converges on the true state.
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
  if (motionReduced()) return false;
  // The marker goes on BEFORE the measure: the element's own entrance (action-menu-pop,
  // stMenuFromAnchor) holds an icon-sized from-state, so a box measured under it is the
  // ICON's. `.dust-driven` kills that keyframe; off again if the dust declines.
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
// …and hands over to it at once on the way out. The caller still owns the real hide/remove
// — the end state never depends on the animation.
export const surfaceOut = (el, point, { ms = SURFACE_OUT_MS } = {}) =>
  playSurface(el, point, { ms, gather: false });

// One-shot "it landed here" flash — restart-safe, so two drops in a row both play.
export function flashLanding(el, cls = 'just-dropped', ms = 900) {
  if (!el?.classList) return;
  clearTimeout(el._landingTimer);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts it
  el.classList.add(cls);
  el._landingTimer = setTimeout(() => el.classList.remove(cls), ms);
}
