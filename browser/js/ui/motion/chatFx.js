import { dustEnabled, motionReduced } from '../motionPrefs.js';
import { disintegrate } from './disintegrate.js';
import { FILTER_DUST_DRIFT, FILTER_DUST_MS, LEAVE_MS, flashLanding, wipeDurationMs } from './enterLeave.js';
import { speckPainter } from './painters.js';
import { dockAwayPoint } from './surfaceMotion.js';
import { surfaceDust } from './surfaces.js';
import { ITEM_DUST_MS, TILE_GATHER_SHARE, cancelDust, retargetDust, scatterGridFor } from './tiles.js';
import { TUNE } from './tune.js';
// ── Materialize: leaveThenRemove reversed, for a freshly-ADDED row ──────────
// Call on the new row right after the render that inserted it: its box expands on the
// short timer while a dust copy gathers over the full wipe; the row stays veiled until
// the motes land (the dust IS the row forming). Resolves once the veil lifts.
export const MATERIALIZE_CLASS = 'materializing';
export const MATERIALIZE_VEIL_CLASS = 'materialize-veil';
const MATERIALIZE_LIFT_CLASS = 'materialize-lift';   // the veil on its way up
// `drift` scales the throw the motes gather FROM: a whole row's default carries them
// most of a hundred pixels, which reads as sand arriving from somewhere else rather than
// the row forming (user report). A caller whose item is short says so.
export function materialize(el, { ms = LEAVE_MS, cols, rows, dustMs = FILTER_DUST_MS,
                                  drift = FILTER_DUST_DRIFT, px = 0 } = {}) {
  const reduced = motionReduced();
  if (!el?.classList || reduced || typeof setTimeout === 'undefined') return Promise.resolve();
  // Freeze the natural height (the row is already laid out) so the expansion has
  // something to animate to — the same trick the leave plays with --leave-h.
  if (el.getBoundingClientRect) {
    const r = el.getBoundingClientRect();
    if (r.height) el.style.setProperty('--enter-h', `${r.height}px`);
  }
  // The dust is filterDust's recipe — the surface gather (visible from the first frame,
  // eased out), on half a row's throw and the filter's short clock. The row gather
  // (reintegrate) was tried here and put the row inside a cloud bigger than itself.
  // cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
  const dusted = cols !== 0 && disintegrate(el, {
    ...(cols ? { cols } : {}), ...(rows ? { rows } : {}), ...(px ? { px } : {}),
    gather: true, ms: dustMs, drift, toBody: true, hostClass: 'dust-forming',
    paintTile: speckPainter(el),
  });
  el.classList.add(MATERIALIZE_CLASS);
  if (dusted) {
    // The veil LIFTS as the motes land, not after them: held at nothing until the first
    // are home (the gather leg), then up to full by the last. A hard veil dropped at the
    // end left a hole — dust gone, nothing, then the row (user report).
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

// ── A chat entry ARRIVES as dust, from its own side ─────────────────────────
// An arrival is a toast arriving (notifications.js): the same speck cloud, gathered out
// of a point off the edge the entry belongs to — the user's messages from the right, the
// assistant's from the left (the LEAVE is still a scatter around the row). The entry is
// held back for the whole flight: the motes ARE it forming, so fading it up underneath
// them would show the message first and the animation after.
// On a short clock of its own: the motes carry no text, so a long answer is unreadable
// until the veil lifts. A fixed FRACTION of the row's flight (520 of the old 900), so
// shortening DISINTEGRATE_MS shortens this with it rather than letting the two meet.
export const CHAT_ENTER_MS = Math.round(ITEM_DUST_MS * 0.58);
export const CHAT_ENTERING_CLASS = 'chat-entering';
// How far off the row's own edge its motes are gathered from. The cloud is clipped to
// the transcript (clipDustToScroller), so a point outside it simply means the sand
// streams in over the edge — exactly what a toast does off the window's.
export const CHAT_ENTER_REACH = TUNE.CHAT_ENTER_REACH;

// The point an arriving entry's dust flies out of: the edge it sits against, read off the
// geometry rather than the role class, so an attachment strip or result card follows the
// message it rides with. Null when unmeasurable — the caller then settles the entry.
export const chatArrivalPoint = (el, r = null, s = null) => {
  r = r || el?.getBoundingClientRect?.();
  s = s || el?.parentElement?.getBoundingClientRect?.();
  if (!r || !s || !(r.width > 0)) return null;
  // Hugging the scroller's right edge more closely than its left ⇒ the user's side.
  return dockAwayPoint(r, (s.right - r.right) <= (r.left - s.left) ? 'right' : 'left',
                       CHAT_ENTER_REACH);
};

// Two frames, so the measure below happens on a SETTLED transcript: frame one is the new
// entries' own layout, frame two is the scroll that follows it (chatView stickToBottom
// pins on a rAF). No rAF (node) ⇒ a macrotask, which is still after the caller returns —
// measuring synchronously would read the pre-scroll box every time.
const afterLayout = (fn) => (typeof requestAnimationFrame === 'function'
  ? requestAnimationFrame(() => requestAnimationFrame(fn))
  : setTimeout(fn, 0));

// Is `el` a whole entry sitting inside its scroller right now? The cloud is
// position:fixed, so the transcript does NOT clip it: an entry still below the fold
// would scatter its motes over the composer under it. Taller than the scroller ⇒ no
// dust either, for the same reason. Pure enough to unit-test.
// `r`/`s` are optional pre-measured rects (trackDust reads each box once per tick).
export const dustFitsScroller = (el, scroller = el?.parentElement, r = null, s = null) => {
  if (!el?.getBoundingClientRect || !scroller?.getBoundingClientRect) return false;
  r = r || el.getBoundingClientRect();
  s = s || scroller.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return false;
  return r.top >= s.top - 1 && r.bottom <= s.bottom + 1;
};

// Confine a flying cloud to its SCROLLER: the tiles translate freely out of an
// `overflow: visible` host, so a gather next to the input rained motes across it
// (reported). Negative insets EXPAND, so motes roam the transcript, never outside it.
const clipDustToScroller = (el, scroller = el?.parentElement, r = null, s = null) => {
  const host = el?.__dustHost;
  if (!host || !scroller?.getBoundingClientRect || !el.getBoundingClientRect) return;
  r = r || el.getBoundingClientRect();
  s = s || scroller.getBoundingClientRect();
  const px = (n) => `${Math.round(n)}px`;
  host.style.clipPath =
    `inset(${px(s.top - r.top)} ${px(r.right - s.right)} ${px(r.bottom - s.bottom)} ${px(s.left - r.left)})`;
};

// Keep a flying cloud pinned to its entry until the motes land: the layer is
// position:fixed but the transcript scrolls under it, so it is re-anchored and
// re-clipped per frame — and dropped (with the entry handed over) if the entry leaves
// the scroller or resizes, since a stale fixed-size cloud would visibly lie about what
// lands. Returns a stop function.
const trackDust = (el, ms, onDrop = () => {}) => {
  if (typeof requestAnimationFrame !== 'function') return () => {};
  let raf = 0;
  let live = true;
  const started = Date.now();
  const shot = el.getBoundingClientRect?.();   // the box the cloud was photographed at
  const step = () => {
    if (!live) return;
    if (Date.now() - started >= ms) return;
    // BOTH rects read once per tick, up front, then handed to every check/helper —
    // the helpers' own reads interleaved with style writes forced a layout per frame.
    const r = el.getBoundingClientRect?.();
    const s = el.parentElement?.getBoundingClientRect?.();
    const resized = !r || !shot
      || Math.abs(r.width - shot.width) > 1 || Math.abs(r.height - shot.height) > 1;
    // Dropping the cloud must HAND THE ENTRY OVER in the same frame: the veil is lifted
    // by a timer at the end of the full flight, so a cancel that only killed the motes
    // left the message invisible with nothing standing in for it until that timer fired.
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
  // No particles ('slide'): the entry has no entrance of its own to fall back on — the
  // cloud WAS it — so it rises in instead (animations/motionModes.css .chat-slide-in).
  if (!dustEnabled()) { flashLanding(el, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS); return Promise.resolve(); }
  const { cols } = scatterGridFor(count, index);   // the burst's budget; the grid is surfaceDust's
  // Veiled from the FIRST frame, before anything is painted: the entry keeps its height
  // (so the transcript grows and scrolls to it as usual) but is never seen ahead of its
  // own motes. Lifted below the moment they land — or at once if none can fly.
  el.classList.add(CHAT_ENTERING_CLASS);
  const unveil = () => el.classList.remove(CHAT_ENTERING_CLASS);
  return new Promise((resolve) => {
    // Fonts first, when the platform offers the promise: a webfont landing after the
    // photograph re-wraps the entry and widens it, and the cloud is then visibly the
    // wrong size for what arrives. Already-loaded fonts resolve this in the same tick,
    // so only the very first arrival of a session ever waits on it.
    const ready = globalThis.document?.fonts?.ready;
    const go = () => afterLayout(() => {
      // cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
      // Past that guard the cloud is the toast's: it sizes its grid to the bubble and
      // paints specks, not clones, so nothing walking the transcript sees a live row.
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
        // The motes have landed, so the entry takes their place in the SAME frame the
        // cloud goes. Left to its own grace period the layer holds its finished state —
        // opaque, at identity — which is an exact second copy over the real entry.
        stop();
        handOver();
      }, CHAT_ENTER_MS);
    });
    if (ready?.then) ready.then(go, go); else go();
  });
}
