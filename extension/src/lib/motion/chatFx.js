// ── Materialize: leaveThenRemove reversed, for a freshly-ADDED row ──────────
// Call right after the render that inserted the row: its box expands on the short timer
// while a dust copy gathers into its final rect, and it stays veiled until the motes land
// (the dust IS the row forming). Decoration only — reduced motion resolves at once.
import { motionReduced, dustEnabled } from '../motionPrefs.js';
import { reintegrate } from './disintegrate.js';
import { LEAVE_MS, flashLanding, wipeDurationMs } from './enterLeave.js';
import { DISINTEGRATE_MS, cancelDust, scatterGridFor } from './tiles.js';
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
