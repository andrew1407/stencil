// ── Leaving ─────────────────────────────────────────────────────────────────
// A row collapses and fades before the caller removes it in the callback. Mirrors the browser twin.
import { motionReduced, dustEnabled, prefersReducedMotion } from '../prefs/motionPrefs.js';
import { disintegrate } from './disintegrate.js';
import { DISINTEGRATE_MS } from './tiles.js';
export const LEAVE_MS = 220;
// Chat entries leave more slowly than a list row (browser twin).
export const CHAT_LEAVE_MS = 260;
export const LEAVING_CLASS = 'leaving';

// `done` always runs: the removal never depends on the animation.
export function leaveThenRemove(el, done = () => {}, { ms = LEAVE_MS, cols, rows } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  if (!el?.classList || motionReduced()) { finish(); return Promise.resolve(); }
  // `height: auto` has no start value to transition from.
  if (el.getBoundingClientRect) {
    const h = el.getBoundingClientRect().height;
    if (h) el.style.setProperty('--leave-h', `${h}px`);
  }
  // The box collapses at once while a copy scatters in its own fixed layer.
  // cols === 0 is scatterGridFor's "fade only".
  if (cols !== 0) disintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}) });
  el.classList.add(LEAVING_CLASS);
  return new Promise((resolve) => setTimeout(() => { finish(); resolve(); }, ms));
}

// Particles fall for DISINTEGRATE_MS after the short collapse (browser motion.js twin).
export const wipeDurationMs = () => {
  if (motionReduced()) return 0;
  return dustEnabled() ? Math.max(LEAVE_MS, DISINTEGRATE_MS) : LEAVE_MS;
};

// begin() opens a hold per playing leave/materialize and returns a settle fn, awaited after the
// removal; finalizeAll() settles everything for a view closing mid-animation.
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

// Under a hold the empty state would land beneath the falling ash. Pure.
export const emptyStateVisible = (count, holding = false) => count === 0 && !holding;

// A filtered-out row is not deleted: no particles, a lighter collapse.
export const FILTER_LEAVE_MS = 150;
export const FILTER_ENTER_MS = 180;
export const FILTER_OUT_CLASS = 'filter-out';
export const FILTER_IN_CLASS = 'filter-in';

export const diffListKeys = (prev = [], next = []) => {
  const had = new Set(prev);
  const has = new Set(next);
  return { entered: next.filter((k) => !had.has(k)), left: prev.filter((k) => !has.has(k)) };
};

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

// begin() before the wipe kills every ghost, end() after the rebuild; keys are
// `el.dataset[keyAttr]`.
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
  const clear = () => {
    for (const g of [...ghosts]) drop(g);
    ghosts = [];
  };

  const begin = () => {
    clear();
    taken = kids().filter((el) => keyOf(el) != null).map((el) => ({ el, key: keyOf(el) }));
    return taken.map((t) => t.key);
  };

  // `skipEnter`: keys the caller animates itself, so two effects never stack.
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

export function flashLanding(el, cls = 'just-dropped', ms = 900) {
  if (!el?.classList) return;
  clearTimeout(el._landingTimer);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts it
  el.classList.add(cls);
  el._landingTimer = setTimeout(() => el.classList.remove(cls), ms);
}
