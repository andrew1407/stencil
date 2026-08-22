// ── Shared UI motion helpers ────────────────────────────────────
// Pure decoration: a missing IntersectionObserver/MutationObserver (node tests,
// old engines) simply means no animation — never a broken or hidden view. CSS
// owns the actual keyframes (css/animations.css); this file only toggles classes.

// Rows only dissolve by the amount the scroller is ALREADY clipping them; a row you can
// see in full is never touched — the grain is finer than a glyph's strokes, and
// decoration must never cost legibility.
export const REVEAL_ITEM_CLASS = 'reveal-item';
export const REVEAL_IN_CLASS = 'reveal-in';
export const REVEAL_ENTERING_CLASS = 'reveal-entering';
// Gates the mask itself: only a row straddling an edge is worth masking.
export const REVEAL_MASKED_CLASS = 'reveal-masked';
export const REVEAL_SMOOTH_CLASS = 'reveal-smooth';
export const REVEAL_NO_TRIGGER_CLASS = 'reveal-no-trigger';
export const REVEAL_ENTER_MS = 700;
// How far the wipe softens into the cut — a share of the ROW, matching animations.css.
export const REVEAL_FEATHER = '10%';

// How dissolved a row spanning [top, bottom) is in a scroller `viewH` tall: 0 while
// wholly on screen, rising with the clipped share, 1 once it is gone. Pure.
export const revealDissolve = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / h;
};

// Which of a row's edges the scroller is ACTUALLY cutting — the only ones the wipe may
// soften: feathering both ends unconditionally sands content sitting on a fully visible
// edge (the hover "…" trigger). Half-pixel slack: flush with an edge is not clipped. Pure.
export const revealFeather = (top, bottom, viewH, feather = REVEAL_FEATHER) => ({
  in: top < -0.5 ? feather : '0%',
  out: bottom > viewH + 0.5 ? feather : '0%',
});

// Text rows (the chat transcript) fade over a FIXED band instead — see the smooth
// option on observeReveal. A constant is also what the bottom-anchored trigger below
// clears, so the two agree by construction.
export const REVEAL_SMOOTH_FEATHER_PX = 12;
export const REVEAL_SMOOTH_FEATHER = `${REVEAL_SMOOTH_FEATHER_PX}px`;

// Distance UP from the row's own bottom where bottom-anchored chrome (the chat "…"
// trigger) must sit to stay inside the slice the SCROLLER is showing, clearing the cut
// edge's fade band — a CONSTANT band, not a share of the row. Pure.
export const revealVisibleBottom = (top, bottom, viewH) => {
  const h = bottom - top;
  if (h <= 0) return 0;
  const vTop = Math.min(h, Math.max(0, -top));
  const vBot = Math.min(h, Math.max(0, viewH - top));
  if (vBot <= vTop) return 0;   // none of the row is on screen — nothing to anchor to
  // Never take more than half the slice for the band: on a sliver, being INSIDE what
  // the user can see beats clearing a band that is already faded anyway.
  const cut = Math.min(bottom > viewH + 0.5 ? REVEAL_SMOOTH_FEATHER_PX : 0, (vBot - vTop) / 2);
  return h - (vBot - cut);
};

// …and whether it may be shown AT ALL: on a short visible slice the pill overflows onto
// the neighbouring bubble, so a row that cannot hold it cleanly gets none; a fully
// visible row always does. `hidden` gives the threshold hysteresis against flutter. Pure.
export const REVEAL_TRIGGER_SIZE = 21;    // .chat-row-menu-btn is 21×21
export const REVEAL_TRIGGER_PAD = 4;      // clearance from the neighbouring bubble
export const REVEAL_TRIGGER_HYSTERESIS = 2;
export const revealTriggerFits = (top, bottom, viewH, hidden = false) => {
  const h = bottom - top;
  if (h <= 0) return false;
  const clipped = top < -0.5 || bottom > viewH + 0.5;
  if (!clipped) return true;
  const vTop = Math.min(h, Math.max(0, -top));
  const vBot = Math.min(h, Math.max(0, viewH - top));
  const cut = Math.min(bottom > viewH + 0.5 ? REVEAL_SMOOTH_FEATHER_PX : 0, (vBot - vTop) / 2);
  const room = REVEAL_TRIGGER_SIZE + REVEAL_TRIGGER_PAD + cut + (hidden ? REVEAL_TRIGGER_HYSTERESIS : 0);
  return vBot - vTop >= room;
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
// disconnect function; safe to call in any environment.
// Deliberately LAZY: geometry is measured once per list change (per-frame rect reads
// force layout and stutter) and the mask mounts only on rows straddling an edge.
// `smooth` is for TEXT rows (the chat transcript): the dot grain eats letters on the
// cut edge, so they get one clean alpha fade — and the "…"-fits class.
export function observeReveal(root, selector, { smooth = false } = {}) {
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
      // Published for EVERY row, masked or not (and before the bailout below): bottom-
      // anchored chrome rides this so a cut row keeps its control against the visible
      // bottom. Written only when it moves — this runs per scroll frame.
      const vb = Math.round(revealVisibleBottom(top, bottom, viewH));
      if (vb !== row.visBottom) {
        row.visBottom = vb;
        row.el.style.setProperty('--visible-bottom', `${vb}px`);
      }
      // …and whether that chrome fits at all, or would be drawn over the neighbour.
      if (smooth) {
        const hide = !revealTriggerFits(top, bottom, viewH, row.noTrigger);
        if (hide !== row.noTrigger) {
          row.noTrigger = hide;
          row.el.classList.toggle(REVEAL_NO_TRIGGER_CLASS, hide);
        }
      }
      if (!masked) continue;
      if (row.h > 0) {
        row.el.style.setProperty('--vis-start', `${(Math.min(1, Math.max(0, -top / row.h)) * 100).toFixed(2)}%`);
        row.el.style.setProperty('--vis-end', `${(Math.min(1, Math.max(0, (viewH - top) / row.h)) * 100).toFixed(2)}%`);
      }
      // …softened only where the scroller really cuts (revealFeather): an edge the user
      // can see in full stays hard, so nothing readable — nor the "…" trigger on the
      // row's bottom edge — is sanded away by a clip happening at the OTHER end.
      const fade = revealFeather(top, bottom, viewH, smooth ? REVEAL_SMOOTH_FEATHER : REVEAL_FEATHER);
      row.el.style.setProperty('--fade-in', fade.in);
      row.el.style.setProperty('--fade-out', fade.out);
      // The soft edge (--vis-*) follows the clipping; the grain has its own ramp so a
      // long message stays readable while you are reading it. Text rows have no grain.
      if (!smooth) row.el.style.setProperty('--dissolve', revealGrain(top, bottom, viewH).toFixed(3));
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
      if (smooth) el.classList.add(REVEAL_SMOOTH_CLASS);
      el.style.setProperty('--dissolve', smooth ? '0' : '1');
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

// ── Menu pop (canvas context menu + chat row menu) ──────────────────────────
// transform-origin for a fixed-position menu popping out of its OPEN point: the
// click clamped into the menu's placed box (post viewport clamping), relative to
// its top-left — so the pop visibly grows out of the cursor. Pure.
export const menuPopOrigin = (x, y, rect) => {
  const cl = (v, max) => Math.min(Math.max(v, 0), max);
  return `${cl(x - rect.left, rect.width)}px ${cl(y - rect.top, rect.height)}px`;
};

// ── FLIP: play an element's NEW box out of the one it had a moment ago ──────
// Used by the fullscreen toggle: entering stretches out of the old box, leaving
// minimises back into the new one. Call AFTER the layout change, with the rect
// measured BEFORE it. Transform-only, so it never re-triggers layout.
// Long and hard-eased-out, so a full-window stretch reads as deliberate.
export const FLIP_MS = 560;
export const FLIP_EASING = 'cubic-bezier(0.16, 1, 0.22, 1)';
// Held on the element for the whole flight. CSS uses it to lift the element above the
// page and stop its scrollbars flashing while it is scaled — without it, an element
// scaled UP out of its in-flow box paints behind the toolbars around it.
export const FLIP_ACTIVE_CLASS = 'flip-active';

// The inverse transform mapping `to` back onto `from`, or null when either box is
// degenerate or the two already match (nothing to play). Pure — unit-tested.
export function flipTransform(from, to) {
  if (!from || !to) return null;
  if (!(from.width > 0 && from.height > 0 && to.width > 0 && to.height > 0)) return null;
  const sx = from.width / to.width;
  const sy = from.height / to.height;
  const dx = from.left - to.left;
  const dy = from.top - to.top;
  const still = Math.abs(sx - 1) < 0.001 && Math.abs(sy - 1) < 0.001
    && Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5;
  if (still) return null;
  return `translate(${dx}px, ${dy}px) scale(${sx}, ${sy})`;
}

export function flipFrom(el, from, { ms = FLIP_MS, activeClass = FLIP_ACTIVE_CLASS } = {}) {
  if (!el?.style || typeof requestAnimationFrame === 'undefined') return;
  if (typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  const clear = () => {
    el.classList?.remove(activeClass);
    el.style.transition = '';
    el.style.transform = '';
    el.style.transformOrigin = '';
    el.style.willChange = '';
  };
  clearTimeout(el._flipTimer);
  clear();
  // Measured with the class ON: it changes overflow (and so whether scrollbars take
  // space), and a box measured without it would be the wrong one to invert.
  el.classList?.add(activeClass);
  const invert = flipTransform(from, el.getBoundingClientRect());
  if (!invert) { clear(); return; }
  el.style.transformOrigin = 'top left';
  el.style.willChange = 'transform';
  el.style.transition = 'none';
  el.style.transform = invert;
  // Two frames: one lands the inverted state, the next starts the transition off it
  // (setting both in the same frame would be coalesced into no animation at all).
  requestAnimationFrame(() => requestAnimationFrame(() => {
    el.style.transition = `transform ${ms}ms ${FLIP_EASING}`;
    el.style.transform = 'none';
  }));
  el._flipTimer = setTimeout(clear, ms + 90);
}

// ── Theme / accent swap ─────────────────────────────────────────────────────
// The new palette floods out of the control that changed it, as a growing circle —
// the native View Transitions API where it exists, a plain cross-fade elsewhere.
// Either way `apply` runs exactly once and synchronously.
// Drives both the wipe (as --swap-ms) and the colour cross-fade. One length across
// all three surfaces (extension accent.js SWAP_MS, desktop themeSwapOverlay.hpp kSwapMs).
export const THEME_SWAP_MS = 280;
export const THEME_SWAP_CLASS = 'theme-swapping';
// Held on <html> only while the view transition captures the new state, so the snapshot
// is the FINAL palette rather than one caught mid colour-transition.
export const THEME_INSTANT_CLASS = 'theme-instant';

// Where the swap starts: the CONTROL that owns the change, resolved by the caller or by
// originOfId below — never the last pointerdown, which can be an unrelated press.
// desktop mainWindow.cpp applyTheme() names the same trap ("the cursor is NOT good
// enough"); the two surfaces agree. No control on screen → the viewport centre.

// The radius that reaches the furthest viewport corner — the circle has to cover the
// whole page, and the corner opposite the origin is the last place it gets to. Pure.
export const swapRadius = (x, y, w, h) => Math.hypot(Math.max(x, w - x), Math.max(y, h - y));

// The wipe rides in as PERCENTAGES of the viewport, never pixels: clip-path resolves them
// against the pseudo-element's own box, and an engine that measures that box in DEVICE
// pixels paints a px origin at half its offset — the circle blooming above and to the left
// of the icon. Percent radii resolve against sqrt(w² + h²) / √2, hence the √2. Pure.
export function swapPercent(x, y, w, h) {
  if (!(w > 0 && h > 0)) return { x: 50, y: 50, r: 150 };   // no viewport to measure (a stub)
  const pc = (v) => Math.round(v * 1000) / 1000;
  return { x: pc((100 * x) / w), y: pc((100 * y) / h),
           r: pc((100 * Math.SQRT2 * swapRadius(x, y, w, h)) / Math.hypot(w, h)) };
}

export function themeSwap(apply, origin = null) {
  if (typeof document === 'undefined') { apply(); return; }
  const root = document.documentElement;
  const reduced = typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;

  if (reduced || typeof document.startViewTransition !== 'function') {
    // No snapshot to wipe: one beat of colour transition instead (a no-op under reduced
    // motion). A document without a classList is a test stub — the palette write still
    // happens; only the decoration is skipped.
    if (!root?.classList) { apply(); return; }
    root.classList.add(THEME_SWAP_CLASS);
    clearTimeout(root._themeSwapTimer);
    apply();
    root._themeSwapTimer = setTimeout(() => root.classList.remove(THEME_SWAP_CLASS), THEME_SWAP_MS);
    return;
  }

  // `origin` may be a POINT or a function that resolves one. A function is re-asked after
  // the palette is written: the control can move between the two (label width changes,
  // toolbar reflow, scroll), and the circle is painted against the NEW page.
  const at = () => {
    const w = window.innerWidth, h = window.innerHeight;
    const p = (typeof origin === 'function' ? origin() : origin) || { x: w / 2, y: h / 2 };
    return swapPercent(p.x, p.y, w, h);
  };
  // Handed to the DECLARATIVE keyframes in animations.css. Scripting the animation from
  // ready.then() instead races the transition's own teardown — it ends as soon as its
  // pseudo-elements have no animations, so the wipe stopped half way.
  const write = ({ x, y, r }) => {
    root.style.setProperty('--swap-x', `${x}%`);
    root.style.setProperty('--swap-y', `${y}%`);
    root.style.setProperty('--swap-r', `${r}%`);
    root.style.setProperty('--swap-ms', `${THEME_SWAP_MS}ms`);
  };
  write(at());
  // Raised BEFORE startViewTransition: the browser drops :hover (synthetic
  // pointerleaves) the moment the transition starts, and hover-latches (ui/toolbar.js
  // logo) tell that synthetic leave from a real one by this class.
  root.classList.add(THEME_INSTANT_CLASS);
  const settle = () => root.classList.remove(THEME_INSTANT_CLASS);
  let t;
  try {
    // The re-ask rides INSIDE the update callback: it is the one place that runs after the
    // palette is written and before the new state is captured, so a control that moved is
    // measured where the wipe will actually be seen.
    t = document.startViewTransition(() => { apply(); write(at()); });
  } catch {
    settle(); apply(); return;   // a sync throw never ran the callback — the write still must happen
  }
  t.finished.then(settle, settle);   // also on a skipped/failed transition
}

// Resolve an id to a swap origin, preferring the element that is actually ON SCREEN:
// the fullscreen layer CLONES the whole toolbar, duplicate ids and all, so
// getElementById can hand back a hidden copy.
export function originOfId(id) {
  if (typeof document?.querySelectorAll !== 'function') return null;
  for (const el of document.querySelectorAll(`[id="${id}"]`)) {
    const o = originOf(el);
    if (o) return o;
  }
  return null;
}

// Clipped out of sight by an ANCESTOR? checkVisibility below cannot see that: a control
// inside a collapsed panel keeps a perfectly good rect and its own `visibility: visible`;
// it is the ancestor's `overflow: hidden` that hides it.
function clippedAway(el, r) {
  if (typeof getComputedStyle !== 'function') return false;   // a stub (tests): nothing to clip
  for (let p = el.parentElement; p; p = p.parentElement) {
    const s = getComputedStyle(p);
    if (s.overflow === 'visible' && s.overflowX === 'visible' && s.overflowY === 'visible') continue;
    const b = p.getBoundingClientRect();
    if (r.right <= b.left || r.left >= b.right || r.bottom <= b.top || r.top >= b.bottom) return true;
  }
  return false;
}

export function originOf(el) {
  const r = el?.getBoundingClientRect?.();
  if (!r || (!r.width && !r.height)) return null;
  if (el.parentElement && clippedAway(el, r)) return null;
  // `visibility: hidden` and `opacity: 0` both leave a perfectly good rect behind, and
  // an off-screen clone keeps its size too — never bloom from one. checkVisibility is
  // Chromium/WebKit-only, so it stays a bonus check.
  if (typeof el.checkVisibility === 'function' &&
      !el.checkVisibility({ visibilityProperty: true, opacityProperty: true, contentVisibilityAuto: true }))
    return null;
  const c = { x: r.left + r.width / 2, y: r.top + r.height / 2 };
  const w = typeof window !== 'undefined' ? window.innerWidth : 0;
  const h = typeof window !== 'undefined' ? window.innerHeight : 0;
  if (w && h && (c.x < 0 || c.y < 0 || c.x > w || c.y > h)) return null;   // scrolled/parked off-screen
  return c;
}

// ── Arriving ────────────────────────────────────────────────────────────────
// The counterpart to leaveThenRemove: dropped content plays IN out of the drop point.
// Without one (the Open dialog, a paste, a fetched URL) there is no place to come
// from, so it keeps the plain landing: a scale-up plus the accent pulse.
export const ARRIVE_MS = 620;
export const ARRIVE_GLOW_CLASS = 'drop-arriving';   // glow only — the FLIP owns the transform
export const ARRIVE_ACTIVE_CLASS = 'arrive-active';
export const LANDING_CLASS = 'drop-landing';        // scale-up + glow, for the no-point case

// The box the arrival flies out of: a small one centred on the drop point. Kept small so
// the content visibly grows out of the cursor, and non-degenerate so flipTransform will
// play it at all. Pure.
export const arrivalBox = (point, size = 96) => ({
  left: point.x - size / 2,
  top: point.y - size / 2,
  width: size,
  height: size * 0.75,
});

// `point` is the drop position in client coordinates ({x, y}), or null.
export function arriveFrom(el, point = null, { ms = ARRIVE_MS } = {}) {
  if (!el?.classList) return;
  const usable = Number.isFinite(point?.x) && Number.isFinite(point?.y);
  if (!usable) { flashLanding(el, LANDING_CLASS, 700); return; }
  // Glow and flight together: the glow is a CSS animation, the flight an inline
  // transform, so the two never fight over the same property.
  flashLanding(el, ARRIVE_GLOW_CLASS, ms + 120);
  flipFrom(el, arrivalBox(point), { ms, activeClass: ARRIVE_ACTIVE_CLASS });
}

// ── Leaving ─────────────────────────────────────────────────────────────────
// A row that is about to be destroyed collapses and fades out first, so a delete
// reads as the row going away rather than the list jumping. The caller does the
// actual removal in the callback — this only buys it the time.
export const LEAVE_MS = 220;
// Chat entries come apart into a finer grid than a list row — the extra particles are
// what make a delete read as "it dissolved" rather than "it blinked out"; the fade
// stays SHORT. Kept here so the panel, the flyout and the extension use one number.
export const CHAT_LEAVE_MS = 260;
// Attachment chips leave on a LONGER clock: the row holds the doomed chip's slot for
// an opening beat (css chipLeave's hold phase) so the dust reads before the survivors
// slide over — the removal must not fire until the collapse really ends
export const CHIP_LEAVE_MS = 420;
export const CHAT_DISINTEGRATE_COLS = 32;
export const CHAT_DISINTEGRATE_ROWS = 16;
export const LEAVING_CLASS = 'leaving';

// A WIPE scatters every row at once and the cost is the sum, not the per-row grid —
// so the mesh is budgeted: one row keeps the full fine grain, a mass clear coarsens
// each row until the total fits. Pure — unit-tested.
export const SCATTER_TILE_BUDGET = 1200;
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

// Play `el` out, then run `done`. Returns a promise resolving after `done`, so a
// caller can await the whole thing. `done` ALWAYS runs, even with no element and
// even under reduced motion — the removal must never depend on the animation.
export function leaveThenRemove(el, done = () => {}, { ms = LEAVE_MS, cols, rows } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  const reduced = typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (!el?.classList || reduced || typeof setTimeout === 'undefined') { finish(); return Promise.resolve(); }
  // Freeze the height so the collapse has something to animate from (rows are
  // auto-height, and `height: auto → 0` does not transition). Width too — chips
  // leave a HORIZONTAL row and collapse sideways (.chat-attach-chip.leaving).
  if (el.getBoundingClientRect) {
    const r = el.getBoundingClientRect();
    if (r.height) el.style.setProperty('--leave-h', `${r.height}px`);
    if (r.width) el.style.setProperty('--leave-w', `${r.width}px`);
  }
  // The row's own box collapses on its short timer (the list closes the gap) while a
  // copy scatters into particles that own their lifetime in their own layer over the
  // page. cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
  if (cols !== 0) disintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}) });
  el.classList.add(LEAVING_CLASS);
  return new Promise((resolve) => setTimeout(() => { finish(); resolve(); }, ms));
}

// How long a wipe REALLY lasts on screen: leaveThenRemove resolves on the short
// collapse while the particles fall for DISINTEGRATE_MS — a caller swapping in a
// PLACEHOLDER must wait for the longer one (same rule as storage.js's GHOST_MS hold).
// 0 under reduced motion: nothing is playing.
export const wipeDurationMs = () => {
  const reduced = typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  return reduced ? 0 : Math.max(LEAVE_MS, DISINTEGRATE_MS);
};

// ── List hold: wipes in flight ──────────────────────────────────────────────
// The projects modal's removal-in-flight hold as a shared factory: begin() returns a
// settle fn that waits out the REAL wipe (wipeDurationMs) then settles exactly once;
// `holding` gates out-of-band re-renders meanwhile; finalizeAll() settles everything
// NOW (a modal closing mid-animation). Timers injectable — unit-tested.
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

// ── Filtering a list ────────────────────────────────────────────────────────
// A row a FILTER drops was not destroyed — it is only out of view — so it must not
// play the delete's scatter (leaveThenRemove + scatterGridFor). It gets a lighter,
// quicker collapse instead, and the rows the filter reveals fade back in on the same
// short clock: enter and exit are the same shape, played opposite ways.
export const FILTER_LEAVE_MS = 150;
export const FILTER_ENTER_MS = 200;
// Small enough that a long list still settles in one beat.
export const FILTER_STAGGER_MS = 16;
// Past this many rows the effect is noise (and a cost) — the rest simply appear.
export const FILTER_MAX_ANIMATED = 16;
export const FILTER_LEAVING_CLASS = 'filter-leaving';
export const FILTER_ENTERING_CLASS = 'filter-entering';

export const motionReduced = () =>
  typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;

// What a filter change does to a list, by row key: which keys it drops, which it
// reveals, and whether the sequence moved at all (`moved` is what a SORT switch has —
// same membership, new order). Pure — unit-tested.
export const filterDelta = (before = [], after = []) => {
  const prev = [...before];
  const next = [...after];
  const prevSet = new Set(prev);
  const nextSet = new Set(next);
  return {
    leaving: prev.filter((k) => !nextSet.has(k)),
    entering: next.filter((k) => !prevSet.has(k)),
    moved: prev.length !== next.length || prev.some((k, i) => k !== next[i]),
  };
};

// One filter animator per list. `keys()` is what the list shows RIGHT NOW, `next()`
// what the pending state WILL show, `render()` rebuilds it, `find(key)` resolves a
// rendered row. Returns a run() for every filter/sort/search handler to call.
// render() ALWAYS runs exactly once per call — under reduced motion, with nothing to
// play, or mid-animation. The rendered set never depends on the decoration.
export const createFilterAnimator = ({
  keys = () => [], next = () => [], render = () => {}, find = () => null,
  leaveMs = FILTER_LEAVE_MS, enterMs = FILTER_ENTER_MS, max = FILTER_MAX_ANIMATED,
  setTimer = setTimeout, reduced = motionReduced,
} = {}) => {
  let seq = 0;
  let flight = 0;   // generation of the leave in flight; 0 when idle
  const rowsFor = (keyList) => keyList.slice(0, max).map((k) => find(k)).filter((el) => el?.classList);

  const playEnter = (rows) => rows.forEach((el, i) => {
    const delay = i * FILTER_STAGGER_MS;
    if (el.style) el.style.animationDelay = `${delay}ms`;
    el.classList.add(FILTER_ENTERING_CLASS);
    setTimer(() => {
      el.classList.remove(FILTER_ENTERING_CLASS);
      if (el.style) el.style.animationDelay = '';
    }, enterMs + delay + 60);
  });

  return () => {
    const gen = ++seq;
    const after = [...next()];
    const { leaving, entering, moved } = filterDelta(keys(), after);
    // Same membership, new order (a sort switch): the whole list settles back in.
    const arriving = (leaving.length || entering.length) ? entering : (moved ? after : []);
    // Reduced motion, nothing to play, or a change landing mid-animation (fast typing
    // in the search box): render straight away — a second overlapping leave would drop
    // rows, and correctness of the rendered set is not negotiable.
    if (flight || reduced() || (!leaving.length && !arriving.length)) {
      render();
      return Promise.resolve({ leaving, entering: arriving });
    }
    const doomed = rowsFor(leaving);
    for (const el of doomed) {
      // Freeze the height, like leaveThenRemove: `height: auto → 0` does not animate.
      const r = el.getBoundingClientRect?.();
      if (r?.height) el.style?.setProperty?.('--leave-h', `${r.height}px`);
      el.classList.add(FILTER_LEAVING_CLASS);
    }
    const settle = () => { render(); playEnter(rowsFor(arriving)); };
    if (!doomed.length) { settle(); return Promise.resolve({ leaving, entering: arriving }); }
    flight = gen;
    return new Promise((resolve) => setTimer(() => {
      flight = 0;
      // Superseded meanwhile? A newer change already rendered the current state; this
      // enter would flash rows that have been on screen for a while.
      if (gen === seq) settle();
      resolve({ leaving, entering: arriving });
    }, leaveMs));
  };
};

// May a list's "nothing here" placeholder show RIGHT NOW? Only when it is truly empty
// AND no wipe is still playing — under a hold the empty state would land beneath the
// falling ash and read as appearing before the removal finished. Pure — unit-tested.
export const emptyStateVisible = (count, holding = false) => count === 0 && !holding;

// ── Swapping a control's face ───────────────────────────────────────────────
// One shared transition for the toggles that rewrite themselves in place — the Draw
// group's Start↔Stop and Line↔Rect. Replacing innerHTML outright cannot animate, so
// the new markup is written FIRST (the DOM is never behind the state, however fast
// the toggling) and the decoration plays around it: the new glyph turns in, the new
// word rises, and the outgoing face leaves as a ghost stacked on top of it. CSS owns
// the keyframes (animations.css .swapping / .swap-ghost).
export const SWAP_MS = 260;
export const SWAP_CLASS = 'swapping';
export const SWAP_GHOST_CLASS = 'swap-ghost';

// The face each element last rendered, and the generation of its in-flight swap.
// Keyed by the ELEMENT: a re-rendered toolbar hands us a fresh node with no entry,
// which paints rather than being skipped as unchanged.
const swapFace = new WeakMap();
const swapGen = new WeakMap();

/**
 * Swap an element's content with the shared transition. `key` identifies the face
 * (markup does not survive a DOM round-trip byte-for-byte, so it is not the trigger).
 * Returns whether the swap ANIMATED — false for an unchanged face, the first paint,
 * or reduced motion, all of which still leave the correct content behind.
 */
export function swapContent(el, html, {
  key = html, ms = SWAP_MS, reduced = motionReduced, setTimer = setTimeout,
} = {}) {
  if (!el) return false;
  const first = !swapFace.has(el);
  if (!first && swapFace.get(el) === key) return false;
  swapFace.set(el, key);
  // Drop a ghost still in flight: it belongs to a face that is now two swaps old.
  for (const g of el.querySelectorAll?.(`.${SWAP_GHOST_CLASS}`) || []) g.remove?.();
  const before = el.innerHTML;
  el.innerHTML = html;
  if (first || reduced()) return false;

  const doc = el.ownerDocument || (typeof document !== 'undefined' ? document : null);
  const ghost = el.appendChild ? doc?.createElement?.('span') : null;
  if (ghost) {
    ghost.className = SWAP_GHOST_CLASS;
    ghost.innerHTML = before;
    ghost.setAttribute?.('aria-hidden', 'true');
    el.appendChild(ghost);
  }
  const gen = (swapGen.get(el) || 0) + 1;
  swapGen.set(el, gen);
  el.classList?.remove(SWAP_CLASS);
  void el.offsetWidth;   // reflow, so the keyframes replay from the top mid-swap
  el.classList?.add(SWAP_CLASS);
  setTimer(() => {
    if (swapGen.get(el) !== gen) return;   // a newer swap owns the element now
    el.classList?.remove(SWAP_CLASS);
    ghost?.remove?.();
  }, ms + 60);
  return true;
}

// ── Pinning a swapping control's box ────────────────────────────────────────
// A face that swaps in place must not resize the button under the cursor, so the Draw
// group's two toggles are width-pinned. The pin is MEASURED, never guessed: each face is
// written into THE BUTTON ITSELF at width:auto and measured there, so the number comes
// from the real font, gap, padding and border — a clone loses whatever its id styles it
// with. That survives a font swap, a zoom and a translated label; a hard-coded rem does
// not. The whole probe is synchronous, so no intermediate face is ever painted, and the
// original markup is put back before returning (the caller's swap sees no change).
// Once per element — a re-rendered toolbar hands over a new node, which re-measures —
// plus one re-measure when webfonts settle, since metrics can change under us.
const facePinned = new WeakSet();
export function pinWidestFace(el, faces, { doc = el?.ownerDocument, force = false } = {}) {
  if (!el || !faces?.length || !el.style || !el.getBoundingClientRect) return 0;
  if (!force && facePinned.has(el)) return 0;
  const html0 = el.innerHTML, width0 = el.style.width;
  el.style.width = 'auto';                 // beats the pin; min-width:max-content is the floor
  let widest = 0;
  for (const html of faces) {
    el.innerHTML = html;
    widest = Math.max(widest, el.getBoundingClientRect().width || 0);
  }
  el.innerHTML = html0;
  el.style.width = width0;
  if (!(widest > 0)) return 0;             // no layout (a stub, a hidden panel): keep the CSS floor
  const px = Math.ceil(widest);
  el.style.width = `${px}px`;
  if (!facePinned.has(el)) {
    facePinned.add(el);
    doc?.fonts?.ready?.then?.(() => { if (el.isConnected !== false) pinWidestFace(el, faces, { doc, force: true }); });
  }
  return px;
}

// ── Disintegration ("the snap") ─────────────────────────────────────────────
// A removed element comes apart: one clone per tile, clipped to its own cell, drifting
// off in a staggered sweep. The tiles live in a FIXED layer over the page — the row is
// collapsing to zero height at the same time and would clip anything inside it.
export const DISINTEGRATE_MS = 900;
// A fine grid — small cells read as ash rather than a broken window; one clone per
// cell, so this is the practical ceiling for a list row. (Matched by the desktop's
// DisintegrateOverlay::kDustCellPx, which sizes its motes in pixels instead.)
export const DISINTEGRATE_COLS = 34;
export const DISINTEGRATE_ROWS = 16;

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

// Where a cell goes and when it starts. `reverse` inverts only the SWEEP (for the
// gather — see reintegrate): the mote that leaves first is the last one home, the same
// rule the canvas dust follows (dustDelay); the flight path is shared. Pure.
export const tileMotion = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS, reverse = false) => {
  const n = tileNoise(cx, cy);
  // A second, decorrelated noise so a mote's SIDEWAYS drift is independent of its fall
  // and its spin — one hash drove all three, which made whole diagonals move as one and
  // read as a sheet tearing rather than a thing coming apart.
  const m = tileNoise(cx + 41, cy + 17);
  // 0 at the TOP row (goes first), 1 at the bottom (goes last): the row crumbles from
  // its top edge downward, the way the cleared image does (ghostOut).
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

// How a tile gets its copy of the element. cloneNode is right for ordinary DOM, but
// NOT for a <canvas>: cloning one yields a blank canvas (the bitmap isn't copied), so
// the caller passes a maker that blits the pixels across instead.
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
// cell COUNT is the frame-budget ceiling. Pure — unit-tested. (Desktop: kDustCellPx.)
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
  if (typeof clearTimeout === 'function') clearTimeout(el.__dustTimer);
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
        // The gather class overrides the scatter's animation with tileGather (the same
        // flight, played home) while inheriting the tile's box/clip rules.
        tile.className = gather ? 'disintegrate-tile reintegrate-tile' : 'disintegrate-tile';
        // A per-cell copy is already only its own slice, so it is POSITIONED; a full
        // clone covers the whole box and is CLIPPED down to its cell instead.
        if (!perCell) tile.style.clipPath = tileInset(cx, cy, cols, rows);
        // Every tile is a FULL-SIZE box clipped to its cell, so the default 50% 50% origin
        // is the row's centre — scaling shrank them all toward the middle, which read as the
        // row imploding rather than coming apart. Each mote turns about its own cell.
        tile.style.transformOrigin = `${((cx + 0.5) / cols) * 100}% ${((cy + 0.5) / rows) * 100}%`;
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
          // A clone REPLAYS its element's CSS entrance animation from t=0 — inside a
          // tile that read as the removed thing flashing back before the scatter.
          // The tile owns all motion; the copy holds still.
          copy.style.animation = 'none';
        }
        tile.appendChild(copy);
        host.appendChild(tile);
      }
    }
    // Appended to the element's own PARENT, not <body>: most row styling is
    // parent-scoped and a body-level clone matches none of it (unstyled, invisible
    // tiles). Still position:fixed, so viewport-anchored, clear of scroller clipping.
    // A SURFACE goes on <body> outright: its own parent (a modal overlay) is about to
    // go display:none under it, and body is last in tree order, so a clone carrying
    // duplicate ids can never shadow the real thing in getElementById.
    (toBody ? document.body : (el.parentElement || document.body)).appendChild(host);
    // …but only if the parent can actually host it: an ancestor with a transform,
    // filter or backdrop-filter becomes the containing block for position:fixed and can
    // re-anchor or clip the layer. Detected by measuring, not by guessing which
    // properties are in play — if the layer did not land where told, re-home on <body>.
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

// ── Reintegration: the snap played backwards ────────────────────────────────
// The same tile layer as disintegrate, but every mote starts where the scatter would
// have flung it and flies HOME (tileGather in animations.css), with the sweep reversed
// so the first mote out is the last one in. Used by materialize below.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });

// ── Surfaces: a window, a panel and a mini popup are dust too ───────────────
// A modal, the chat panel and the ⋯/context popups play the SAME scatter a deleted
// row does — only every mote flies INTO (or out of) the point that owns the surface:
// the icon that opened it, the click a context menu grew from, the edge a docked
// panel slides off. Origin and direction are exactly what the old scale had; what
// changed is that the flight is rendered as particles instead of a moving rectangle.
export const SURFACE_IN_MS = 420;    // was modalFromIcon's 0.42s
export const SURFACE_OUT_MS = 340;   // …and modalToIcon's 0.34s (ui/base.js CLOSE_MS)
// Coarser than a row's 7px grain: a window is tens of times the area, and the mote
// COUNT — not the mote size — is what a frame has to pay for.
export const SURFACE_MOTE_PX = 8;
export const SURFACE_COLS = 40;
export const SURFACE_ROWS = 30;      // 1200 motes — the wipe's own ceiling (SCATTER_TILE_BUDGET)
export const SURFACE_SPREAD = 34;    // how far a mote may fan off its line to the point
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

// Where a DOCKED panel's dust comes from, and goes back to: far out past the edge it
// is docked to, so the motes stream along the panel's own slide direction — the same
// "where it comes from" the slide had. A float has an icon instead, so: null. Pure.
export const dockAwayPoint = (rect, dock, reach = 2.2) => {
  if (!rect) return null;
  const cx = rect.left + rect.width / 2;
  const cy = rect.top + rect.height / 2;
  if (dock === 'left') return { x: cx - rect.width * reach, y: cy };
  if (dock === 'right') return { x: cx + rect.width * reach, y: cy };
  if (dock === 'top') return { x: cx, y: cy - rect.height * reach };
  if (dock === 'bottom') return { x: cx, y: cy + rect.height * reach };
  return null;
};

// May this surface afford a REAL clone per cell — what makes a chat message come apart
// into pieces of itself? A small menu can; a settings window is hundreds of nodes over
// hundreds of cells and the PRODUCT is what would stall the frame. Past the budget the
// motes are flat specks in the surface's own colours, which at mote size is nearly all
// a clone would show anyway. Pure — unit-tested.
export const SURFACE_CLONE_NODE_BUDGET = 12000;
export const surfaceClones = (nodes, tiles) =>
  Math.max(1, nodes || 0) * Math.max(0, tiles || 0) <= SURFACE_CLONE_NODE_BUDGET;

// Flat speck in the surface's own colours; the rim cells take its border instead, so
// the cloud keeps the window's outline for the first frames.
const speckMaker = (el) => {
  const cs = typeof getComputedStyle === 'function' ? getComputedStyle(el) : null;
  const blank = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);
  const fill = blank(cs?.backgroundColor) ? 'var(--bg-container)' : cs.backgroundColor;
  const edge = blank(cs?.borderTopColor) ? fill : cs.borderTopColor;
  return (_el, { cx, cy, cols, rows }) => {
    const d = document.createElement('div');
    d.className = 'dust-mote';
    const n = tileNoise(cx, cy);
    d.style.background = (cx === 0 || cy === 0 || cx === cols - 1 || cy === rows - 1) ? edge : fill;
    d.style.opacity = (0.62 + n * 0.38).toFixed(2);
    // Grains of ONE size read as a mosaic; the spread is what makes it sand. On the
    // copy, not the tile — the tile's own transform is the flight.
    d.style.transform = `scale(${(0.72 + n * 0.5).toFixed(2)})`;
    return d;
  };
};

// A menu is position:fixed. Inside a tile it must lay out from the tile's own corner
// instead of re-anchoring to the viewport — or, once the tile is moving, to the tile.
const surfaceClone = (el) => {
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
    perCell: !clone, makeCopy: clone ? surfaceClone : speckMaker(el),
  });
};

// Drop whatever a surface has in flight — the cloud AND the classes driving its own
// opacity — leaving the end state untouched. Every open/close begins here, so a
// double-clicked menu or a swept-past modal always converges on the true state.
export function settleSurface(el) {
  if (!el?.classList) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__surfaceTimer);
  el.__surfaceTimer = null;
  el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
  el.style?.removeProperty?.('--dust-ms');
  cancelDust(el);
}

const playSurface = (el, point, { ms, gather }) => {
  if (!el?.classList) return false;
  settleSurface(el);
  if (motionReduced()) return false;
  // The marker goes on BEFORE the measure: the element's own entrance (modalFromIcon,
  // chatSlide*, menuPop) FILLS an icon-sized from-state, so a box read under it is the
  // icon's box — the same trap `modal-measuring` dodges in ui/base.js. Off again if the
  // dust declines, so a surface that never plays it keeps its old CSS entrance.
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

// ── Materialize: leaveThenRemove reversed, for a freshly-ADDED row ──────────
// Call on the new row right after the render that inserted it: its box expands on the
// short timer while a dust copy gathers over the full wipe; the row stays veiled until
// the motes land (the dust IS the row forming). Resolves once the veil lifts.
export const MATERIALIZE_CLASS = 'materializing';
export const MATERIALIZE_VEIL_CLASS = 'materialize-veil';
export function materialize(el, { ms = LEAVE_MS, cols, rows } = {}) {
  const reduced = typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (!el?.classList || reduced || typeof setTimeout === 'undefined') return Promise.resolve();
  // Freeze the natural height (the row is already laid out) so the expansion has
  // something to animate to — the same trick the leave plays with --leave-h.
  if (el.getBoundingClientRect) {
    const r = el.getBoundingClientRect();
    if (r.height) el.style.setProperty('--enter-h', `${r.height}px`);
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

// ── Ghosting a canvas out ───────────────────────────────────────────────────
// Clearing the canvas is synchronous, so the pixels are copied into a throwaway canvas
// laid over the real one and THAT is animated away. Call immediately BEFORE the clear;
// a failed ghost must never block the clear itself.
export const GHOST_MS = 1100;
// Dust motes are sized on SCREEN, not as a share of the image: a fixed grid over a
// big canvas gives big rectangles, which is what stopped it reading as dust.
export const DUST_CELL_PX = 6;
// Halving the cell quadruples the count, so the ceiling has to rise with it — but it
// still exists: past this, a frame costs more than the effect is worth and the grid is
// thinned back instead.
export const DUST_MAX_PARTICLES = 7000;

// The canvas disintegrates as DUST, downward — a particle system on ONE canvas, not
// the DOM tiles a row uses: thousands of elements would not survive a frame. Each mote
// is a small source rect blitted at an offset, so it carries real pixels.
// When a mote's clock starts, as a fraction of the flight: OUT runs TOP-DOWN; IN is
// the same sweep REVERSED, so the mote that left first is the last one home. Pure.
export const dustDelay = (cy, rows, n, reverse = false) => {
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  return (reverse ? 1 - progress : progress) * 0.55 + n * 0.12;
};

// Ease-out for the arrival: a mote covers most of the distance early and settles into
// place, rather than crawling the last few pixels. Pure.
export const dustEase = (k) => 1 - (1 - k) ** 3;

// Returns true when the dust is actually playing — the same contract as ghostIn, and
// for the same reason: the caller hides the emptied editor only while there are motes
// in front of it. Under reduced motion (or any other bail-out) it said nothing, and the
// editor was held blank for the whole 1.1s with nothing to look at.
export function ghostOut(canvas, { ms = GHOST_MS } = {}) {
  if (typeof document === 'undefined' || !canvas?.width || !canvas.height) return false;
  if (typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches) return false;
  if (typeof requestAnimationFrame === 'undefined' || !canvas.parentElement) return false;
  try {
    const r = canvas.getBoundingClientRect();
    if (r.width < 8 || r.height < 8) return false;

    // Grid sized so every mote is about DUST_CELL_PX on screen, then thinned if that
    // would exceed the particle ceiling.
    let cols = Math.max(1, Math.round(r.width / DUST_CELL_PX));
    let rows = Math.max(1, Math.round(r.height / DUST_CELL_PX));
    while (cols * rows > DUST_MAX_PARTICLES) { cols = Math.ceil(cols / 1.1); rows = Math.ceil(rows / 1.1); }

    // The source is snapshotted once — the real canvas is cleared moments from now.
    const snap = document.createElement('canvas');
    snap.width = canvas.width;
    snap.height = canvas.height;
    snap.getContext('2d').drawImage(canvas, 0, 0);

    const dpr = window.devicePixelRatio || 1;
    const stage = document.createElement('canvas');
    stage.className = 'canvas-dust';
    stage.width = Math.round(r.width * dpr);
    stage.height = Math.round(r.height * dpr);
    const host = canvas.parentElement.getBoundingClientRect();
    stage.style.left = `${r.left - host.left}px`;
    stage.style.top = `${r.top - host.top}px`;
    stage.style.width = `${r.width}px`;
    stage.style.height = `${r.height}px`;
    canvas.parentElement.appendChild(stage);

    const ctx = stage.getContext('2d');
    ctx.scale(dpr, dpr);
    const sw = snap.width / cols, sh = snap.height / rows;
    const dw = r.width / cols, dh = r.height / rows;
    const parts = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        const n = tileNoise(cx, cy);
        // Decorrelated from `n`: one hash driving fall AND drift slid the picture apart
        // in diagonal sheets; a separate hash for the x-axis is what makes it scatter.
        const m = tileNoise(cx + 41, cy + 17);
        parts.push({
          sx: cx * sw, sy: cy * sh,
          x: cx * dw, y: cy * dh,
          delay: dustDelay(cy, rows, n),
          dx: (m - 0.5) * 58,          // fan sideways — signed, so both ways
          dy: 26 + n * 46,             // …and fall
          rot: (m - 0.5) * 1.4,
        });
      }
    }

    const started = performance.now();
    const step = (now) => {
      const t = (now - started) / ms;
      if (t >= 1) { stage.remove(); return; }
      ctx.clearRect(0, 0, r.width, r.height);
      for (const p of parts) {
        const pt = (t - p.delay) / Math.max(0.05, 1 - p.delay);
        if (pt >= 1) continue;                       // this mote is gone
        const k = pt <= 0 ? 0 : pt;
        ctx.save();
        ctx.globalAlpha = 1 - k;
        ctx.translate(p.x + dw / 2 + k * p.dx, p.y + dh / 2 + k * k * p.dy);
        ctx.rotate(k * p.rot);
        const s = 1 - k * 0.35;
        ctx.drawImage(snap, p.sx, p.sy, sw, sh,
                      (-dw / 2) * s, (-dh / 2) * s, dw * s + 0.6, dh * s + 0.6);
        ctx.restore();
      }
      requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
    setTimeout(() => stage.remove(), ms + 400);   // belt and braces if rAF is throttled
    return true;
  } catch {
    return false;   // decoration only — the clear still happens
  }
}

// ── Canvas dust, the other way (ghostIn) ────────────────────────────────────
// The arrival is the clear PLAYED BACKWARDS: same grid, noise and fall, sweep
// reversed, so the picture builds bottom-to-top. It deliberately does NOT stream in
// from the drop point: the stage canvas is only as big as the image, and a mote pulled
// toward the cursor spends the whole flight outside it, clipped away.
// Returns true when the dust is actually playing: the caller only hides the real
// canvas if it is, or a bail-out (reduced motion, tiny canvas, no rAF) would leave a
// blank editor.
// Is anything actually drawn here? Sampled, not exhaustive — a stride wide enough to be
// cheap on a big canvas and fine enough that no real image reads as empty.
export function hasPixels(ctx, w, h, stride = 41) {
  if (!ctx?.getImageData || w < 1 || h < 1) return false;
  const d = ctx.getImageData(0, 0, w, h).data;
  for (let p = 3; p < d.length; p += 4 * stride) if (d[p] > 0) return true;
  return false;
}

export function ghostIn(canvas, { ms = GHOST_MS } = {}) {
  if (typeof document === 'undefined' || !canvas?.width || !canvas.height) return false;
  if (typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches) return false;
  if (typeof requestAnimationFrame === 'undefined' || !canvas.parentElement) return false;
  try {
    const r = canvas.getBoundingClientRect();
    if (r.width < 8 || r.height < 8) return false;

    let cols = Math.max(1, Math.round(r.width / DUST_CELL_PX));
    let rows = Math.max(1, Math.round(r.height / DUST_CELL_PX));
    while (cols * rows > DUST_MAX_PARTICLES) { cols = Math.ceil(cols / 1.1); rows = Math.ceil(rows / 1.1); }

    // Snapshot what the motes are made of. Unlike ghostOut the original survives — it
    // is merely hidden while the dust settles — but the stage still needs its own copy.
    const snap = document.createElement('canvas');
    snap.width = canvas.width;
    snap.height = canvas.height;
    const snapCtx = snap.getContext('2d');
    snapCtx.drawImage(canvas, 0, 0);
    // …and it has to HAVE something: snapshotted before the load has painted, the dust
    // is invisible motes in front of a canvas the caller is holding hidden. Saying no
    // makes the caller fall back to the plain flight instead of animating nothing.
    if (!hasPixels(snapCtx, snap.width, snap.height)) return false;

    const dpr = window.devicePixelRatio || 1;
    const stage = document.createElement('canvas');
    stage.className = 'canvas-dust';
    stage.width = Math.round(r.width * dpr);
    stage.height = Math.round(r.height * dpr);
    const host = canvas.parentElement.getBoundingClientRect();
    stage.style.left = `${r.left - host.left}px`;
    stage.style.top = `${r.top - host.top}px`;
    stage.style.width = `${r.width}px`;
    stage.style.height = `${r.height}px`;
    canvas.parentElement.appendChild(stage);

    const ctx = stage.getContext('2d');
    ctx.scale(dpr, dpr);
    const sw = snap.width / cols, sh = snap.height / rows;
    const dw = r.width / cols, dh = r.height / rows;
    const parts = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        const n = tileNoise(cx, cy);
        // The fall inverted: ghostOut carries a mote DOWN and away, so the arrival starts
        // it down there and lifts it home. Same fan sideways, same magnitudes.
        parts.push({ sx: cx * sw, sy: cy * sh, x: cx * dw, y: cy * dh,
                     delay: dustDelay(cy, rows, n, true),
                     dx: (n - 0.5) * 26, dy: 26 + n * 46, rot: (n - 0.5) * 0.9 });
      }
    }

    const started = performance.now();
    const step = (now) => {
      const t = (now - started) / ms;
      if (t >= 1) { stage.remove(); return; }
      ctx.clearRect(0, 0, r.width, r.height);
      for (const p of parts) {
        const pt = (t - p.delay) / Math.max(0.05, 1 - p.delay);
        if (pt <= 0) continue;                        // this mote has not set off yet
        const e = dustEase(Math.min(1, pt));
        const away = 1 - e;                           // 1 = still out there, 0 = home
        ctx.save();
        ctx.globalAlpha = e;
        ctx.translate(p.x + dw / 2 + away * p.dx, p.y + dh / 2 + away * p.dy);
        ctx.rotate(away * p.rot);
        const s = 1 - away * 0.35;
        ctx.drawImage(snap, p.sx, p.sy, sw, sh,
                      (-dw / 2) * s, (-dh / 2) * s, dw * s + 0.6, dh * s + 0.6);
        ctx.restore();
      }
      requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
    setTimeout(() => stage.remove(), ms + 400);   // belt and braces if rAF is throttled
    return true;
  } catch {
    return false;   // decoration only — the image is already loaded either way
  }
}

// ── The one place an image's ARRIVAL is played ──────────────────────────────
// Every route that puts a picture on the canvas ends here: a fresh file load AND a
// reopened project. It used to live inline in the loader only, so opening a saved
// project painted the image with no motion at all.
// Dust gather when ghostIn can run (the canvas waits behind it under
// ASSEMBLING_CLASS), the drop-point flight when it can't. Returns which one played,
// so the wiring is testable without a DOM.
export const ASSEMBLING_CLASS = 'canvas-assembling';
export const CLEARING_CLASS = 'canvas-clearing';

export function playCanvasArrival(canvas, { from = null, viewport, container, ghost = ghostIn } = {}) {
  const doc = typeof document !== 'undefined' ? document : null;
  // Both looked up by ID: the loader used to reach for the viewport by CLASS, and the
  // fullscreen layer clones the shell — two elements answering to one selector.
  const vp = viewport !== undefined ? viewport : doc?.getElementById('canvas-viewport') || null;
  const box = container !== undefined ? container : doc?.getElementById('canvas-container') || null;
  if (ghost(canvas)) {
    if (vp) flashLanding(vp, ASSEMBLING_CLASS, GHOST_MS);
    return 'dust';
  }
  // ghostIn declined (reduced motion, a canvas too small, nothing painted yet) — the
  // plain landing still runs, and is itself inert under reduced motion.
  arriveFrom(box, from);
  return 'flight';
}

// One-shot "it landed here" flash — restart-safe, so dropping twice in a row
// replays the animation instead of silently doing nothing the second time.
export function flashLanding(el, cls = 'drop-landing', ms = 700) {
  if (!el?.classList) return;
  // One timer PER CLASS, not per element: the canvas viewport carries two of these,
  // and a shared handle let the second call cancel the first one's removal — leaving a
  // class that hides the canvas on it for good.
  const timers = el._landingTimers || (el._landingTimers = {});
  clearTimeout(timers[cls]);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts it
  el.classList.add(cls);
  timers[cls] = setTimeout(() => el.classList.remove(cls), ms);
}
