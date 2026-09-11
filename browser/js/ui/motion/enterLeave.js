import { dustEnabled, motionReduced } from '../motionPrefs.js';
import { disintegrate } from './disintegrate.js';
import { flipFrom } from './flip.js';
import { speckPainter } from './painters.js';
import { DISINTEGRATE_MS, MOTE_PX, scatterGridFor } from './tiles.js';
import { TUNE } from './tune.js';
// ── Arriving ────────────────────────────────────────────────────────────────
// The counterpart to leaveThenRemove: dropped content plays IN out of the drop point.
// Without one (the Open dialog, a paste, a fetched URL) there is no place to come
// from, so it keeps the plain landing: a scale-up plus the accent pulse.
const ARRIVE_MS = TUNE.ARRIVE_MS;
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
export const LEAVE_MS = TUNE.LEAVE_MS;
// Chat entries come apart into a finer grid than a list row — the extra particles are
// what make a delete read as "it dissolved" rather than "it blinked out"; the fade
// stays SHORT. Kept here so the panel, the flyout and the extension use one number.
export const CHAT_LEAVE_MS = TUNE.CHAT_LEAVE_MS;
// Attachment chips leave on a LONGER clock: the row holds the doomed chip's slot for
// an opening beat (css chipLeave's hold phase) so the dust reads before the survivors
// slide over — the removal must not fire until the collapse really ends
export const CHIP_LEAVE_MS = TUNE.CHIP_LEAVE_MS;

export const LEAVING_CLASS = 'leaving';


// Play `el` out, then run `done`. Returns a promise resolving after `done`, so a
// caller can await the whole thing. `done` ALWAYS runs, even with no element and
// even under reduced motion — the removal must never depend on the animation.
export function leaveThenRemove(el, done = () => {}, { ms = LEAVE_MS, cols, rows, dustMs = 0,
                                                      drift = 1, px = 0 } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  const reduced = motionReduced();
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
  if (cols !== 0) disintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}),
                                    ...(dustMs ? { ms: dustMs } : {}), ...(px ? { px } : {}),
                                    ...(drift === 1 ? {} : { drift }) });
  el.classList.add(LEAVING_CLASS);
  return new Promise((resolve) => setTimeout(() => { finish(); resolve(); }, ms));
}

// How long a wipe REALLY lasts on screen: leaveThenRemove resolves on the short
// collapse while the particles fall for DISINTEGRATE_MS — a caller swapping in a
// PLACEHOLDER must wait for the longer one (same rule as storage.js's GHOST_MS hold).
// 0 under reduced motion: nothing is playing.
export const wipeDurationMs = (dustMs = 0) => {
  if (motionReduced()) return 0;
  if (dustMs) return dustEnabled() ? Math.max(LEAVE_MS, dustMs) : LEAVE_MS;
  // No dust ('slide') means no particle tail to wait out — the row's own collapse is
  // the whole wipe, and a caller holding for the longer clock would just stall.
  return dustEnabled() ? Math.max(LEAVE_MS, DISINTEGRATE_MS) : LEAVE_MS;
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
// A filter is a question re-answered, not a removal: dropped rows just vanish with the
// rebuild, and the whole effect belongs to the rows that are LEFT, which assemble anew.
export const FILTER_ENTER_MS = TUNE.FILTER_ENTER_MS;
// Small enough that a long list still settles in one beat.
const FILTER_STAGGER_MS = TUNE.FILTER_STAGGER_MS;
// Past this many rows the effect is noise (and a cost) — the rest simply appear.
const FILTER_MAX_ANIMATED = TUNE.FILTER_MAX_ANIMATED;
export const FILTER_ENTERING_CLASS = 'filter-entering';
// The kept rows form from anonymous specks (speckPainter) on a shorter, non-destructive
// throw — a filter is a view change, never mistakable for a deletion's scatter.
export const FILTER_DUST_MS = TUNE.FILTER_DUST_MS;
export const FILTER_DUST_DRIFT = TUNE.FILTER_DUST_DRIFT;
// `index`/`count` share ONE mesh budget across every row a change moves (scatterGridFor),
// so a filter that leaves a dozen rows costs about what one deletion does. Past the
// budget's row ceiling a row simply fades, as it always did.
// `box` is an optional pre-measured rect, so a burst of rows can be measured in one
// read pass before any cloud's writes start (createFilterAnimator's playEnter).
export const filterDust = (el, index = 0, count = 1, box = null) => {
  const { cols, rows } = scatterGridFor(count, index);
  if (!cols || !el?.getBoundingClientRect || motionReduced()) return false;
  return disintegrate(el, {
    cols, rows, gather: true, ms: FILTER_DUST_MS, drift: FILTER_DUST_DRIFT, px: MOTE_PX,
    toBody: true, hostClass: 'dust-forming', paintTile: speckPainter(el), box,
  });
};

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
//
// render() ALWAYS runs exactly once per call, FIRST and synchronously: the new answer is
// on screen before any decoration starts, so nothing about what you can see waits on an
// effect and a burst of keystrokes can never leave a stale set behind. What plays is the
// arrival of the rows that are LEFT — every one of them, because the filtered set IS the
// thing that changed, not just the rows that happen to be new to it.
export const createFilterAnimator = ({
  keys = () => [], next = () => [], render = () => {}, find = () => null,
  enterMs = FILTER_ENTER_MS, max = FILTER_MAX_ANIMATED,
  setTimer = setTimeout, reduced = motionReduced,
} = {}) => {
  const rowsFor = (keyList) => keyList.slice(0, max).map((k) => find(k)).filter((el) => el?.classList);

  const playEnter = (rows) => {
    // Every rect in ONE read pass, then clouds/classes in a write pass — interleaving
    // the two forced a layout per row on every keystroke.
    const boxes = rows.map((el) => el.getBoundingClientRect?.() || null);
    rows.forEach((el, i) => {
      const delay = i * FILTER_STAGGER_MS;
      if (el.style) el.style.animationDelay = `${delay}ms`;
      filterDust(el, i, rows.length, boxes[i]);
      el.classList.add(FILTER_ENTERING_CLASS);
      setTimer(() => {
        el.classList.remove(FILTER_ENTERING_CLASS);
        if (el.style) el.style.animationDelay = '';
      }, enterMs + delay + 60);
    });
  };

  return () => {
    const after = [...next()];
    const { leaving, entering, moved } = filterDelta(keys(), after);
    render();
    // A keystroke that narrows nothing must not flash the list: only a change that
    // really moved the answer is worth replaying.
    const arriving = (leaving.length || entering.length || moved) ? after : [];
    if (!reduced()) playEnter(rowsFor(arriving));
    return Promise.resolve({ leaving, entering: arriving });
  };
};

// May a list's "nothing here" placeholder show RIGHT NOW? Only when it is truly empty
// AND no wipe is still playing — under a hold the empty state would land beneath the
// falling ash and read as appearing before the removal finished. Pure — unit-tested.
export const emptyStateVisible = (count, holding = false) => count === 0 && !holding;



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
