import { markIn, markOut } from './motion.js';

// ── Form controls: what a tick does when it comes and goes ──────────────────
// A checkbox's checked indicator (the accent fill + its tick) is a MARK, so it forms
// out of motes and comes apart into them like every other mark in the app
// (js/ui/motion.js markIn / markOut). The box itself never moves — only what it
// displays does — so the veil suppresses the checked look alone and leaves the outline
// standing throughout.
//
// The trigger is ONE delegated listener, for the same reason the desktop has one
// application-wide event filter (desktop/src/support/controlSwap.hpp): checkboxes are
// built in a dozen components and no call site should have to know. Programmatic
// changes fire no `change` event, so the settings mirror calls setChecked() instead —
// Alt+P and the context-menu twin animate exactly as a click on the box does.

// A control opts out with this attribute (the desktop's kNoControlSwapProperty).
export const NO_SWAP_ATTR = 'data-no-mark-dust';

// What the checked state PAINTS, resolved off the live element rather than guessed:
// the app-wide box fills with the accent, the context menu's twin with the theme text
// colour, and a future one with whatever its rule says. Cached, because by the time an
// UNcheck is seen the element paints nothing at all any more — and where there is no
// cache yet, the look is probed by ticking the box for the length of one synchronous
// style read (no event fires, and nothing can repaint in between).
const checkedInk = (el) => {
  const read = () => getComputedStyle(el).backgroundColor;
  const blank = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);
  if (el.checked) { el.__markInk = read(); return el.__markInk; }
  if (el.__markInk) return el.__markInk;
  // A radio cannot be poked like that — ticking one unticks its group.
  if (el.type !== 'checkbox') return getComputedStyle(el).borderTopColor;
  el.checked = true;
  const ink = read();
  el.checked = false;
  el.__markInk = blank(ink) ? '' : ink;
  return el.__markInk;
};

// Play the indicator's arrival or departure. `on` is the state the box has JUST
// reached, so a tick GATHERS and an untick SCATTERS. Anything that cannot be dusted —
// reduced motion, a hidden box, a pill whose indicator is `width: 0` — simply does
// nothing: the state itself has already changed, which is the part that must not wait.
export function playCheckDust(el, on) {
  if (!el?.getBoundingClientRect || el.hasAttribute?.(NO_SWAP_ATTR)) return false;
  const ink = checkedInk(el);
  if (!ink) return false;   // an indicator that paints nothing has nothing to scatter
  const paint = { fill: ink, edge: ink };
  return on ? markIn(el, { paint }) : markOut(el, { paint });
}

// Set a checkbox from CODE and animate the change, or leave it alone when there is no
// change to show. The settings mirror (core/settingsController.js) writes through here.
export function setChecked(el, value) {
  if (!el) return;
  const on = !!value;
  if (el.checked === on) return;
  el.checked = on;
  playCheckDust(el, on);
}

// A check GLYPH that is written in and out of a span (the context menu's rows carry the
// tick as markup, not as an indicator). Same sand; the span is empty when it is off, so
// the arrival needs no veil beyond the one markIn already applies.
export function swapCheckGlyph(el, html) {
  if (!el) return;
  const had = !!el.innerHTML;
  const has = !!html;
  if (had === has) { el.innerHTML = html; return; }
  if (has) { el.innerHTML = html; markIn(el); return; }
  markOut(el);
  el.innerHTML = html;
}

// One delegated listener for every user gesture on a checkbox or radio anywhere in the
// page — including a click on the <label> that owns it, which fires `change` on the box.
export function installControlSwap(root = typeof document !== 'undefined' ? document : null) {
  if (!root?.addEventListener || root.__markSwapWired) return;
  root.__markSwapWired = true;
  root.addEventListener('change', (e) => {
    const el = e.target;
    if (!el || (el.type !== 'checkbox' && el.type !== 'radio')) return;
    playCheckDust(el, el.checked);
    if (el.type !== 'radio') return;
    // A radio TAKES the mark from whichever sibling had it, and that one loses its tick
    // with no event of its own — so the group is swept here.
    if (el.name)
      for (const other of root.querySelectorAll(`input[type=radio][name="${CSS.escape(el.name)}"]`))
        if (other !== el && other.__markWasOn) { playCheckDust(other, false); other.__markWasOn = false; }
    el.__markWasOn = true;
  }, true);
}
