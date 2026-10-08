// ── Materialize: leaveThenRemove reversed, for a freshly-ADDED row ──────────
// The box expands on the short timer while a dust copy gathers into its final rect; the row
// stays veiled until the motes land. Reduced motion resolves at once.
import { motionReduced, dustEnabled } from '../prefs/motionPrefs.js';
import { reintegrate } from './disintegrate.js';
import { LEAVE_MS, flashLanding, wipeDurationMs } from './enterLeave.js';
import { DISINTEGRATE_MS, cancelDust, scatterGridFor } from './tiles.js';
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
  }, dusted ? wipeDurationMs() : ms));
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

// A chat entry's only entrance is its dust, so in 'slide' it rises
// (browser css/animations/motionModes.css chatRiseIn twin).
export const CHAT_SLIDE_CLASS = 'chat-slide-in';
export const CHAT_SLIDE_MS = 320;
export function chatIn(el, count = 1, index = 0, { host = null } = {}) {
  if (!el?.classList || motionReduced()) return Promise.resolve();
  if (!dustEnabled()) { flashLanding(el, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS); return Promise.resolve(); }
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
