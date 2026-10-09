import { dustEnabled, motionMs, motionReduced } from './motionPrefs.js';
import { disintegrate, scrollerOf } from './disintegrate.js';
import { flipFrom } from './flip.js';
import { speckPainter } from './surface/painters.js';
import { DISINTEGRATE_MS, MOTE_PX, TILE_GATHER_SHARE, scatterGridFor } from './surface/tiles.js';
import { TUNE } from './tune.js';
// Dropped content plays in out of the drop point; with no point it keeps the plain landing.
const ARRIVE_MS = TUNE.ARRIVE_MS;
export const ARRIVE_GLOW_CLASS = 'drop-arriving';   // glow only — the FLIP owns the transform
export const ARRIVE_ACTIVE_CLASS = 'arrive-active';
export const LANDING_CLASS = 'drop-landing';        // scale-up + glow, for the no-point case

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
// The glow is a CSS animation, the flight an inline transform: they never share a property.
  flashLanding(el, ARRIVE_GLOW_CLASS, ms + 120);
  flipFrom(el, arrivalBox(point), { ms, activeClass: ARRIVE_ACTIVE_CLASS });
}

// A row collapses and fades before removal; the caller removes it in the callback.
export const LEAVE_MS = TUNE.LEAVE_MS;
// Chat entries use a finer grid than a list row; one number for the panel, flyout and extension.
export const CHAT_LEAVE_MS = TUNE.CHAT_LEAVE_MS;
// Chips leave on a longer clock: the row holds the slot for css chipLeave's hold phase.
export const CHIP_LEAVE_MS = TUNE.CHIP_LEAVE_MS;

export const LEAVING_CLASS = 'leaving';


// `done` ALWAYS runs, even with no element or under reduced motion.
export function leaveThenRemove(el, done = () => {}, { ms = LEAVE_MS, cols, rows, dustMs = 0,
                                                      drift = 1, px = 0 } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  const reduced = motionReduced();
  if (!el?.classList || reduced || typeof setTimeout === 'undefined') { finish(); return Promise.resolve(); }
// Freeze the box: `height: auto → 0` does not transition; chips collapse sideways too.
  if (el.getBoundingClientRect) {
    const r = el.getBoundingClientRect();
    if (r.height) el.style.setProperty('--leave-h', `${r.height}px`);
    if (r.width) el.style.setProperty('--leave-w', `${r.width}px`);
  }
// cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
  if (cols !== 0) disintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}),
                                    ...(dustMs ? { ms: dustMs } : {}), ...(px ? { px } : {}),
                                    ...(drift === 1 ? {} : { drift }), clip: true });
  el.classList.add(LEAVING_CLASS);
  return new Promise((resolve) => setTimeout(() => { finish(); resolve(); }, motionMs(ms)));
}

// The particles fall past the short collapse, so a caller swapping in a placeholder waits for the
// longer one; a mode that flies NOTHING waits for nothing — the collapse it awaited IS the wipe.
export const wipeDurationMs = (dustMs = 0) => {
  if (!dustEnabled()) return 0;
  return Math.max(LEAVE_MS, dustMs || DISINTEGRATE_MS);
};

// Removal-in-flight hold: begin() returns a settle fn that waits out the real wipe and
// settles once; `holding` gates re-renders meanwhile; finalizeAll() settles everything now.
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

// A filter is a re-answered question: the rows that are LEFT assemble anew.
export const FILTER_ENTER_MS = TUNE.FILTER_ENTER_MS;
// Small enough that a long list still settles in one beat.
const FILTER_STAGGER_MS = TUNE.FILTER_STAGGER_MS;
// Past this many rows the effect is noise (and a cost) — the rest simply appear.
const FILTER_MAX_ANIMATED = TUNE.FILTER_MAX_ANIMATED;
export const FILTER_ENTERING_CLASS = 'filter-entering';
// A shorter, non-destructive throw, never mistakable for a deletion's scatter.
export const FILTER_DUST_MS = TUNE.FILTER_DUST_MS;
// A row the list GAINS forms half again as briskly as a filter's — the desktop's arrival
// clock, shared (support/theme/filterFade.hpp ROW_ARRIVE_MS).
export const ROW_ARRIVE_MS = Math.round(FILTER_DUST_MS / 1.5);
// Waits out the falling leg (the share by which the leaving motes have mostly travelled) —
// far short of the whole DISINTEGRATE_MS wipe, or the arrival's motes are lost in it.
export const ROW_ARRIVE_DELAY_MS = Math.round(DISINTEGRATE_MS * TILE_GATHER_SHARE);
export const FILTER_DUST_DRIFT = TUNE.FILTER_DUST_DRIFT;
// A row its motes gather into is veiled until the first are home and whole by the last: the classes
// control/chatFx.js materialize wears (dust.css).
const VEIL_CLASS = 'materialize-veil';
const VEIL_LIFT_CLASS = 'materialize-lift';
// `index`/`count` share one mesh budget across the rows (scatterGridFor); `box` is an
// optional pre-measured rect so a burst is measured in one read pass.
export const filterDust = (el, index = 0, count = 1, box = null) => {
  const { cols, rows } = scatterGridFor(count, index);
  if (!cols || !el?.getBoundingClientRect || motionReduced()) return false;
  return disintegrate(el, {
    cols, rows, gather: true, ms: FILTER_DUST_MS, drift: FILTER_DUST_DRIFT, px: MOTE_PX,
    toBody: true, hostClass: 'dust-forming', paintTile: speckPainter(el), box, clip: true,
  });
};

// The same light throw, scattering: a row a filter drops dissolves where it sat. The cloud is a
// copy on <body>, so the row itself goes with the re-render that drops it.
export const filterDustOut = (el, index = 0, count = 1, box = null) => {
  const { cols, rows } = scatterGridFor(count, index);
  if (!cols || !el?.getBoundingClientRect || motionReduced()) return false;
  return disintegrate(el, {
    cols, rows, ms: FILTER_DUST_MS, drift: FILTER_DUST_DRIFT, px: MOTE_PX,
    toBody: true, paintTile: speckPainter(el), box, clip: true,
  });
};

// Which keys a filter change drops and reveals; `moved` is what a sort switch has.
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

// render() runs exactly once per call, synchronously, so a burst of keystrokes never leaves a
// stale set; what plays is the arrival of every row that is left, and with `leave` the rows dropped
// dissolve first (measured before the render takes them).
export const createFilterAnimator = ({
  keys = () => [], next = () => [], render = () => {}, find = () => null,
  enterMs = FILTER_ENTER_MS, max = FILTER_MAX_ANIMATED,
  setTimer = setTimeout, reduced = motionReduced, leave = false, dustOut = filterDustOut,
} = {}) => {
  // Only the rows in the list's view play, so the mesh budget goes to what can be seen.
  const rowsFor = (keyList) => {
    const els = keyList.map((k) => find(k)).filter((el) => el?.classList);
    const view = els.length ? scrollerOf(els[0])?.getBoundingClientRect?.() : null;
    const seen = view ? els.filter((el) => {
      const r = el.getBoundingClientRect?.();
      return !r || (r.bottom > view.top && r.top < view.bottom);
    }) : els;
    return seen.slice(0, max);
  };

  // A row its motes gather into stays veiled under them; one with no motes (past the budget,
  // or a mode that flies none) fades up on the CSS entrance instead.
  const veil = (el) => {
    const lift = Math.round(FILTER_DUST_MS * TILE_GATHER_SHARE);
    el.classList.add(VEIL_CLASS);
    el.style?.setProperty?.('--veil-fade', `${Math.max(1, FILTER_DUST_MS - lift)}ms`);
    setTimer(() => el.classList.add(VEIL_LIFT_CLASS), motionMs(lift));
    setTimer(() => {
      el.classList.remove(VEIL_CLASS, VEIL_LIFT_CLASS);
      el.style?.removeProperty?.('--veil-fade');
    }, motionMs(FILTER_DUST_MS) + 60);
  };
  const playEnter = (rows) => {
// One read pass, then one write pass: interleaving forced a layout per row.
    const boxes = rows.map((el) => el.getBoundingClientRect?.() || null);
    rows.forEach((el, i) => {
      if (filterDust(el, i, rows.length, boxes[i])) { veil(el); return; }
      const delay = i * FILTER_STAGGER_MS;
      if (el.style) el.style.animationDelay = `${delay}ms`;
      el.classList.add(FILTER_ENTERING_CLASS);
      setTimer(() => {
        el.classList.remove(FILTER_ENTERING_CLASS);
        if (el.style) el.style.animationDelay = '';
      }, motionMs(enterMs + delay) + 60);
    });
  };

  const playLeave = (rows) => {
    const boxes = rows.map((el) => el.getBoundingClientRect?.() || null);
    rows.forEach((el, i) => dustOut(el, i, rows.length, boxes[i]));
  };

  return () => {
    const after = [...next()];
    const { leaving, entering, moved } = filterDelta(keys(), after);
    if (leave && leaving.length && !reduced()) playLeave(rowsFor(leaving));
    render();
// Only a change that really moved the answer replays the list.
    const arriving = (leaving.length || entering.length || moved) ? after : [];
    if (!reduced()) playEnter(rowsFor(arriving));
    return Promise.resolve({ leaving, entering: arriving });
  };
};

// Under a hold the empty state would land beneath the falling ash.
export const emptyStateVisible = (count, holding = false) => count === 0 && !holding;



// Restart-safe: dropping twice replays the animation.
export function flashLanding(el, cls = 'drop-landing', ms = 700) {
  if (!el?.classList) return;
// One timer per class, not per element: the canvas viewport carries two of these.
  const timers = el._landingTimers || (el._landingTimers = {});
  clearTimeout(timers[cls]);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts it
  el.classList.add(cls);
  timers[cls] = setTimeout(() => el.classList.remove(cls), motionMs(ms));
}
