import { dustEnabled, motionReduced } from '../../motionPrefs.js';
import { disintegrate } from '../disintegrate.js';
import { FILTER_DUST_DRIFT, FILTER_DUST_MS, LEAVE_MS, flashLanding, wipeDurationMs } from '../enterLeave.js';
import { speckPainter } from '../surface/painters.js';
import { dockAwayPoint } from '../surface/surfaceMotion.js';
import { surfaceDust } from '../surface/surfaces.js';
import { ITEM_DUST_MS, TILE_GATHER_SHARE, cancelDust, retargetDust, scatterGridFor } from '../surface/tiles.js';
import { TUNE } from '../tune.js';
// leaveThenRemove reversed, for a freshly added row: the box expands on the short timer
// while a dust copy gathers; the row stays veiled until the motes land.
export const MATERIALIZE_CLASS = 'materializing';
export const MATERIALIZE_VEIL_CLASS = 'materialize-veil';
const MATERIALIZE_LIFT_CLASS = 'materialize-lift';   // the veil on its way up
export function materialize(el, { ms = LEAVE_MS, cols, rows, dustMs = FILTER_DUST_MS,
                                  drift = FILTER_DUST_DRIFT, px = 0 } = {}) {
  const reduced = motionReduced();
  if (!el?.classList || reduced || typeof setTimeout === 'undefined') return Promise.resolve();
  if (el.getBoundingClientRect) {
    const r = el.getBoundingClientRect();
    if (r.height) el.style.setProperty('--enter-h', `${r.height}px`);
  }
// filterDust's recipe; cols === 0 is the budget's "fade only".
  const dusted = cols !== 0 && disintegrate(el, {
    ...(cols ? { cols } : {}), ...(rows ? { rows } : {}), ...(px ? { px } : {}),
    gather: true, ms: dustMs, drift, toBody: true, hostClass: 'dust-forming',
    paintTile: speckPainter(el),
  });
  el.classList.add(MATERIALIZE_CLASS);
  if (dusted) {
// The veil lifts as the motes land: held until the first are home, full by the last.
    el.classList.add(MATERIALIZE_VEIL_CLASS);
    const lift = Math.round(dustMs * TILE_GATHER_SHARE);
    el.style?.setProperty?.('--veil-fade', `${Math.max(1, dustMs - lift)}ms`);
    setTimeout(() => el.classList.add(MATERIALIZE_LIFT_CLASS), lift);
  }
  return new Promise((resolve) => setTimeout(() => {
    el.classList.remove(MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, MATERIALIZE_LIFT_CLASS);
    el.style?.removeProperty?.('--veil-fade');
    resolve();
  }, dusted ? wipeDurationMs(dustMs) : ms));
}

// A fixed fraction of the row's flight, so shortening DISINTEGRATE_MS shortens this with it.
export const CHAT_ENTER_MS = Math.round(ITEM_DUST_MS * 0.58);
export const CHAT_ENTERING_CLASS = 'chat-entering';
// The cloud is clipped to the transcript, so the sand streams in over its edge.
export const CHAT_ENTER_REACH = TUNE.CHAT_ENTER_REACH;

// Null when unmeasurable.
export const chatArrivalPoint = (el, r = null, s = null) => {
  r = r || el?.getBoundingClientRect?.();
  s = s || el?.parentElement?.getBoundingClientRect?.();
  if (!r || !s || !(r.width > 0)) return null;
  // Hugging the scroller's right edge more closely than its left ⇒ the user's side.
  return dockAwayPoint(r, (s.right - r.right) <= (r.left - s.left) ? 'right' : 'left',
                       CHAT_ENTER_REACH);
};

// Two frames: the new entries' layout, then the scroll that follows it (chatView
// stickToBottom pins on a rAF). No rAF (node) ⇒ a macrotask.
const afterLayout = (fn) => (typeof requestAnimationFrame === 'function'
  ? requestAnimationFrame(() => requestAnimationFrame(fn))
  : setTimeout(fn, 0));

// The cloud is position:fixed, so the transcript does not clip it; `r`/`s` are pre-measured rects.
export const dustFitsScroller = (el, scroller = el?.parentElement, r = null, s = null) => {
  if (!el?.getBoundingClientRect || !scroller?.getBoundingClientRect) return false;
  r = r || el.getBoundingClientRect();
  s = s || scroller.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return false;
  return r.top >= s.top - 1 && r.bottom <= s.bottom + 1;
};

// Confine a cloud to its scroller; negative insets expand, so motes never leave the transcript.
const clipDustToScroller = (el, scroller = el?.parentElement, r = null, s = null) => {
  const host = el?.__dustHost;
  if (!host || !scroller?.getBoundingClientRect || !el.getBoundingClientRect) return;
  r = r || el.getBoundingClientRect();
  s = s || scroller.getBoundingClientRect();
  const px = (n) => `${Math.round(n)}px`;
  host.style.clipPath =
    `inset(${px(s.top - r.top)} ${px(r.right - s.right)} ${px(r.bottom - s.bottom)} ${px(s.left - r.left)})`;
};

// Re-anchor and re-clip a flying cloud per frame (the transcript scrolls under the fixed
// layer); drop it, handing the entry over, if the entry leaves the scroller or resizes.
const trackDust = (el, ms, onDrop = () => {}) => {
  if (typeof requestAnimationFrame !== 'function') return () => {};
  let raf = 0;
  let live = true;
  const started = Date.now();
  const shot = el.getBoundingClientRect?.();   // the box the cloud was photographed at
  const step = () => {
    if (!live) return;
    if (Date.now() - started >= ms) return;
// Both rects read once per tick; interleaved reads and writes forced a layout per frame.
    const r = el.getBoundingClientRect?.();
    const s = el.parentElement?.getBoundingClientRect?.();
    const resized = !r || !shot
      || Math.abs(r.width - shot.width) > 1 || Math.abs(r.height - shot.height) > 1;
// The drop must hand the entry over in the same frame, or the veiled message stays
// invisible until the end-of-flight timer.
    if (!dustFitsScroller(el, el.parentElement, r, s) || resized) {
      cancelDust(el); live = false; onDrop(); return;
    }
    retargetDust(el, r);
    clipDustToScroller(el, el.parentElement, r, s);
    raf = requestAnimationFrame(step);
  };
  raf = requestAnimationFrame(step);
  return () => { live = false; if (raf) cancelAnimationFrame(raf); };
};

export const CHAT_SLIDE_CLASS = 'chat-slide-in';
export const CHAT_SLIDE_MS = TUNE.CHAT_SLIDE_MS;
export function chatIn(el, count = 1, index = 0) {
  if (!el?.classList || motionReduced() || typeof setTimeout === 'undefined') return Promise.resolve();
// No particles: the cloud WAS the entrance, so the entry rises in instead (.chat-slide-in).
  if (!dustEnabled()) { flashLanding(el, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS); return Promise.resolve(); }
  const { cols } = scatterGridFor(count, index);   // the burst's budget; the grid is surfaceDust's
// Veiled from the first frame, so the entry is never seen ahead of its own motes.
  el.classList.add(CHAT_ENTERING_CLASS);
  const unveil = () => el.classList.remove(CHAT_ENTERING_CLASS);
  return new Promise((resolve) => {
// Fonts first: a webfont landing after the photograph re-wraps the entry and the cloud
// is the wrong size. Loaded fonts resolve in the same tick.
    const ready = globalThis.document?.fonts?.ready;
    const go = () => afterLayout(() => {
// cols === 0 is the budget's "fade only"; the cloud is the toast's — specks, not clones.
      const flying = cols !== 0 && dustFitsScroller(el)
        && surfaceDust(el, chatArrivalPoint(el), { ms: CHAT_ENTER_MS, gather: true });
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
// The entry takes the motes' place in the same frame the cloud goes; left to its grace
// period the layer is an exact second copy over the real entry.
        stop();
        handOver();
      }, CHAT_ENTER_MS);
    });
    if (ready?.then) ready.then(go, go); else go();
  });
}
