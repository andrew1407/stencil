// A chat entry's motion without or beside its cloud: materialize (the slot opening while the motes gather),
// the entry's pour out of its tail home and back (chatIn, pourIn, chatPourAway) and Transition's grow.
// Browser twin: ui/motion/control/chatFx.js; the clocks are motion.json's through tests/lib/motion/motionClocks.test.js.
import { motionReduced, dustEnabled, motionMode } from '../prefs/motionPrefs.js';
import { reintegrate } from './disintegrate.js';
import { LEAVE_MS, flashLanding, rowClock, wipeDurationMs } from './enterLeave.js';
import { DISINTEGRATE_MS, cancelDust, scatterGridFor } from './tiles.js';
import { snapshot } from './genie/shot.js';
import { playGenie } from './genie/flight.js';
export const MATERIALIZE_CLASS = 'materializing';
export const MATERIALIZE_VEIL_CLASS = 'materialize-veil';
export function materialize(el, { ms = LEAVE_MS, cols, rows } = {}) {
  if (!el?.classList || motionReduced()) return Promise.resolve();
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
  }, dusted ? wipeDurationMs() : rowClock(ms)));
}

// A fraction of the row's flight (browser motion.js twin): the motes carry no text.
export const CHAT_ENTER_MS = Math.round(DISINTEGRATE_MS * 0.58);
export const CHAT_ENTERING_CLASS = 'chat-entering';

// Two frames, so the measure sees a settled transcript. No rAF (node) ⇒ a macrotask.
const afterLayout = (fn) => (typeof requestAnimationFrame === 'function'
  ? requestAnimationFrame(() => requestAnimationFrame(fn))
  : setTimeout(fn, 0));

// The cloud is position:fixed, so the transcript does not clip it.
const rectInScroller = (r, s) => !!(r && s && r.width > 0 && r.height > 0
  && r.top >= s.top - 1 && r.bottom <= s.bottom + 1);

export const dustFitsScroller = (el, scroller = el?.parentElement) => {
  if (!el?.getBoundingClientRect || !scroller?.getBoundingClientRect) return false;
  return rectInScroller(el.getBoundingClientRect(), scroller.getBoundingClientRect());
};

// Insets are signed against the host's border box — negative expands it.
const dustClipInset = (r, s) => {
  const px = (n) => `${Math.round(n)}px`;
  return `inset(${px(s.top - r.top)} ${px(r.right - s.right)} ${px(r.bottom - s.bottom)} ${px(s.left - r.left)})`;
};
const clipDustToScroller = (el, scroller = el?.parentElement) => {
  const host = el?.__dustHost;
  if (!host || !scroller?.getBoundingClientRect || !el.getBoundingClientRect) return;
  host.style.clipPath = dustClipInset(el.getBoundingClientRect(), scroller.getBoundingClientRect());
};

// Re-anchored per frame as the transcript scrolls under the fixed layer. Returns a stop fn.
const trackDust = (el, ms, onDrop = () => {}) => {
  if (typeof requestAnimationFrame !== 'function') return () => {};
  let raf = 0;
  let live = true;
  const started = Date.now();
  // A subject that resizes mid-flight cannot be re-photographed, so the copy is dropped.
  const shot = el.getBoundingClientRect?.();
  let last = {};   // the anchor/clip already written — an unchanged frame writes nothing
  const step = () => {
    if (!live) return;
    if (Date.now() - started >= ms) return;
    // Reads first, writes batched, or every cloud forces a layout per frame.
    const r = el.getBoundingClientRect?.();
    const s = el.parentElement?.getBoundingClientRect?.();
    const resized = !r || !shot || Math.abs(r.width - shot.width) > 1 || Math.abs(r.height - shot.height) > 1;
    // Dropping the cloud hands the entry over in the same frame, or nothing is on screen.
    if (!rectInScroller(r, s) || resized) { cancelDust(el); live = false; onDrop(); return; }
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

// The fallback entrance where neither the pour nor the grow plays: the row rises into place
// (browser css/animations/motionModes.css chatRiseIn twin).
export const CHAT_SLIDE_CLASS = 'chat-slide-in';
export const CHAT_SLIDE_MS = 320;
// Transition's entrance: the bubble grows out of its tail corner (animations/motionModes.css chatGrowIn);
// Sliding pours it (pourIn), with the side slide (CHAT_SIDE_CLASS) as its fallback. Both stay inside the chat (browser chatFx.js).
export const CHAT_GROW_CLASS = 'chat-grow-in';
export const CHAT_SIDE_CLASS = 'chat-side-in';
export const CHAT_SIDE_MS = 420;   // ms, motion.json CHAT_SIDE_MS: the side slide, as the browser's
// Where Sliding pours an entry from and back into: a strip on the transcript's edge on the entry's own side,
// level with its tail at the bubble's foot (browser chatFx.js chatTailHome).
export const chatTailHome = (el, r = el?.getBoundingClientRect?.(), view = el?.parentElement?.getBoundingClientRect?.()) => {
  if (!r || !view || !(r.width > 0)) return null;
  const isRight = (view.right - r.right) <= (r.left - view.left);
  return { left: isRight ? view.right - 3 : view.left + 1, top: Math.max(view.top, r.bottom - 18), width: 2, height: 16 };
};
// Sliding's pour (browser chatFx.js pourIn): veiled, photographed, poured out of its tail home and clipped
// to the transcript; the side slide when no picture can be made.
const pourIn = (el) => {
  el.classList.add(CHAT_ENTERING_CLASS);
  const slide = () => { el.classList.remove(CHAT_ENTERING_CLASS); flashLanding(el, CHAT_SIDE_CLASS, CHAT_SIDE_MS); };
  afterLayout(() => {
    const view = el.parentElement?.getBoundingClientRect?.();
    const r = el.getBoundingClientRect?.();
    if (!view || !(r?.width > 0 && r.height > 0)) { slide(); return; }
    snapshot(el).then((shot) => {
      if (!shot || !el.isConnected) { slide(); return; }
      const from = chatTailHome(el, r, view);
      playGenie(shot, r, from, { opening: true, ms: CHAT_SIDE_MS, clip: view,
        done: () => el.classList.remove(CHAT_ENTERING_CLASS) });
    }, slide);
  });
};
// The entry without a cloud, by mode: Sliding pours the row in, Transition grows it, any other slides it.
const ENTER_WITHOUT_DUST = Object.freeze({
  slide: (el) => pourIn(el),
  transition: (el) => flashLanding(el, CHAT_GROW_CLASS, CHAT_SLIDE_MS),
});
const enterWithoutDust = (el) => (ENTER_WITHOUT_DUST[motionMode()] ?? ((row) => flashLanding(row, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS)))(el);

export function chatIn(el, count = 1, index = 0, { host = null } = {}) {
  if (!el?.classList || motionReduced()) return Promise.resolve();
  if (!dustEnabled()) { enterWithoutDust(el); return Promise.resolve(); }
  const { cols, rows } = scatterGridFor(count, index);
  // Veiled from the first frame, keeping its height, never seen ahead of its motes.
  el.classList.add(CHAT_ENTERING_CLASS);
  const unveil = () => el.classList.remove(CHAT_ENTERING_CLASS);
  return new Promise((resolve) => {
    // A webfont landing after the photograph re-wraps the entry; loaded fonts resolve at once.
    const ready = globalThis.document?.fonts?.ready;
    const go = () => afterLayout(() => {
      // `host` is the ancestor the bubble rules are scoped to (browser: `.chat-msg` is standalone).
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
        // Swapped in the same frame the cloud goes, or the layer is a second copy.
        stop();
        handOver();
      }, CHAT_ENTER_MS);
    });
    if (ready?.then) ready.then(go, go); else go();
  });
}

// …and a clear pours each one back into it, the row hidden at once while its picture flies (browser chat/leave.js).
export const chatPourAway = (el) => {
  if (motionMode() !== 'slide' || motionReduced() || !el?.getBoundingClientRect) return false;
  const view = el.parentElement?.getBoundingClientRect?.();
  const r = el.getBoundingClientRect();
  const home = chatTailHome(el, r, view);
  if (!home || !(r.height > 0)) return false;
  snapshot(el).then((shot) => { if (shot) playGenie(shot, r, home, { opening: false, ms: CHAT_SIDE_MS, clip: view }); }, () => {});
  return true;
};
