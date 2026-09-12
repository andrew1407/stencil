// ── Leaving ─────────────────────────────────────────────────────────────────
// A row about to be destroyed collapses and fades first, so a delete reads as the row
// going away rather than the list jumping. The caller still does the removal in the
// callback — this only buys it the time. Mirrors the browser twin.
import { motionReduced, dustEnabled, prefersReducedMotion } from '../motionPrefs.js';
import { disintegrate } from './disintegrate.js';
import { DISINTEGRATE_MS } from './tiles.js';
export const LEAVE_MS = 220;
// Chat entries leave more slowly than a list row (browser twin): the extra time reads
// as "it dissolved" rather than "it blinked out".
export const CHAT_LEAVE_MS = 260;
export const LEAVING_CLASS = 'leaving';

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

// One-shot "it landed here" flash — restart-safe, so two drops in a row both play.
export function flashLanding(el, cls = 'just-dropped', ms = 900) {
  if (!el?.classList) return;
  clearTimeout(el._landingTimer);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts it
  el.classList.add(cls);
  el._landingTimer = setTimeout(() => el.classList.remove(cls), ms);
}
