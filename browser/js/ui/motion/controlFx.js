import { motionReduced } from '../motionPrefs.js';
import { TUNE } from './tune.js';
// ── Replaying a glyph's state motion on a switch ─────────────────────────────
// A control that switches wears one of these classes for one flight, so the parts
// whose visibility IS that state (the mic's sound waves) arrive with the hover's own
// swell when it goes ON, and fly out past the edges when it goes OFF, rather than just
// appearing / vanishing. CSS: `.voice-waves-in` / `.voice-waves-out` in animations.css.
const WAVES_IN_CLASS = 'voice-waves-in';
const WAVES_OUT_CLASS = 'voice-waves-out';
const WAVES_FLIGHT_MS = TUNE.WAVES_FLIGHT_MS;
export function replayWaves(el, on, { setTimer = setTimeout } = {}) {
  if (!el?.classList) return;
  el.classList.remove(WAVES_IN_CLASS, WAVES_OUT_CLASS);
  void el.offsetWidth;   // restart the keyframes on a quick double switch
  const cls = on ? WAVES_IN_CLASS : WAVES_OUT_CLASS;
  el.classList.add(cls);
  // The class comes off once the flight is over — but not under a resting pointer: the
  // hover trigger would then take the waves back and play its own swell a second time.
  const done = () => {
    if (el.matches?.(':hover')) { el.addEventListener('pointerleave', () => el.classList.remove(cls), { once: true }); return; }
    el.classList.remove(cls);
  };
  setTimer(done, WAVES_FLIGHT_MS);
}

// ── Swapping a control's face ───────────────────────────────────────────────
// One shared transition for the toggles that rewrite themselves in place — the Draw
// group's Start↔Stop and Line↔Rect. Replacing innerHTML outright cannot animate, so
// the new markup is written FIRST (the DOM is never behind the state, however fast
// the toggling) and the decoration plays around it: the new glyph turns in, the new
// word rises, and the outgoing face leaves as a ghost stacked on top of it. CSS owns
// the keyframes (animations.css .swapping / .swap-ghost).
export const SWAP_MS = TUNE.SWAP_MS;
export const SWAP_CLASS = 'swapping';
export const SWAP_GHOST_CLASS = 'swap-ghost';

// The face each element last rendered, and the generation of its in-flight swap.
// Keyed by the ELEMENT: a re-rendered toolbar hands us a fresh node with no entry,
// which paints rather than being skipped as unchanged.
const swapFace = new WeakMap();
const swapGen = new WeakMap();

/**
 * Swap an element's content with the shared transition. `key` identifies the face
 * (markup does not survive a DOM round-trip byte-for-byte, so it is not the trigger).
 * Returns whether the swap ANIMATED — false for an unchanged face, the first paint,
 * or reduced motion, all of which still leave the correct content behind.
 */
export function swapContent(el, html, {
  key = html, ms = SWAP_MS, reduced = motionReduced, setTimer = setTimeout,
} = {}) {
  if (!el) return false;
  const first = !swapFace.has(el);
  if (!first && swapFace.get(el) === key) return false;
  swapFace.set(el, key);
  // Drop a ghost still in flight: it belongs to a face that is now two swaps old.
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

// ── Pinning a swapping control's box ────────────────────────────────────────
// A face that swaps in place must not resize the button under the cursor, so the Draw
// group's two toggles are width-pinned. The pin is MEASURED, never guessed: each face is
// written into THE BUTTON ITSELF at width:auto and measured there, so the number comes
// from the real font, gap, padding and border — a clone loses whatever its id styles it
// with. That survives a font swap, a zoom and a translated label; a hard-coded rem does
// not. The whole probe is synchronous, so no intermediate face is ever painted, and the
// original markup is put back before returning (the caller's swap sees no change).
// Once per element — a re-rendered toolbar hands over a new node, which re-measures —
// plus one re-measure when webfonts settle, since metrics can change under us.
// `prop` picks which box the number lands in: `width` PINS the control (the Draw toggles,
// whose two faces are the only widths it will ever hold), `minWidth` gives it a FLOOR that
// CSS may still stretch — a dropdown whose option list can grow past `max` later.
// `max` caps the pin, so one very long option cannot push a control past its row.
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
