import { motionReduced } from '../../motionPrefs.js';
import { TUNE } from '../tune.js';
// A switching control wears one of these for one flight, so the parts whose visibility
// IS the state (the mic's waves) arrive and leave (animations/voice.css).
const WAVES_IN_CLASS = 'voice-waves-in';
const WAVES_OUT_CLASS = 'voice-waves-out';
const WAVES_FLIGHT_MS = TUNE.WAVES_FLIGHT_MS;
export function replayWaves(el, on, { setTimer = setTimeout } = {}) {
  if (!el?.classList) return;
  el.classList.remove(WAVES_IN_CLASS, WAVES_OUT_CLASS);
  void el.offsetWidth;   // restart the keyframes on a quick double switch
  const cls = on ? WAVES_IN_CLASS : WAVES_OUT_CLASS;
  el.classList.add(cls);
// Not under a resting pointer: the hover trigger would replay its own swell.
  const done = () => {
    if (el.matches?.(':hover')) { el.addEventListener('pointerleave', () => el.classList.remove(cls), { once: true }); return; }
    el.classList.remove(cls);
  };
  setTimer(done, WAVES_FLIGHT_MS);
}

// Toggles that rewrite themselves in place: the new markup is written FIRST, then the outgoing
// face leaves as a ghost stacked over it (animations/controls.css .swapping / .swap-ghost).
export const SWAP_MS = TUNE.SWAP_MS;
export const SWAP_CLASS = 'swapping';
export const SWAP_GHOST_CLASS = 'swap-ghost';

// Keyed by the element: a re-rendered toolbar's fresh node paints instead of being skipped.
const swapFace = new WeakMap();
const swapGen = new WeakMap();

// `key` identifies the face; returns whether the swap ANIMATED.
export function swapContent(el, html, {
  key = html, ms = SWAP_MS, reduced = motionReduced, setTimer = setTimeout,
} = {}) {
  if (!el) return false;
  const first = !swapFace.has(el);
  if (!first && swapFace.get(el) === key) return false;
  swapFace.set(el, key);
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

// Measured, never guessed: each face is written into the button itself at width:auto,
// synchronously. `prop` is `width` (a pin) or `minWidth` (a floor CSS may stretch); `max` caps it.
const facePinned = new WeakSet();
export function pinWidestFace(el, faces, { doc = el?.ownerDocument, force = false,
                                           prop = 'width', max = Infinity } = {}) {
  if (!el || !faces?.length || !el.style || !el.getBoundingClientRect) return 0;
  if (!force && facePinned.has(el)) return 0;
  const html0 = el.innerHTML, pin0 = el.style[prop];
  el.style[prop] = 'auto';                 // beats the pin; min-width:max-content is the floor
  let widest = 0;
  for (const html of faces) {
    el.innerHTML = html;
    widest = Math.max(widest, el.getBoundingClientRect().width || 0);
  }
  el.innerHTML = html0;
  el.style[prop] = pin0;
  if (!(widest > 0)) return 0;             // no layout (a stub, a hidden panel): keep the CSS floor
  const px = Math.min(Math.ceil(widest), max);
  el.style[prop] = `${px}px`;
  if (!facePinned.has(el)) {
    facePinned.add(el);
    doc?.fonts?.ready?.then?.(() => {
      if (el.isConnected !== false) pinWidestFace(el, faces, { doc, force: true, prop, max });
    });
  }
  return px;
}
