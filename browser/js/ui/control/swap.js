import { markIn, markOut } from '../motion.js';

// A checkbox's checked indicator is a mark: it forms and scatters like every mark
// (motion.js markIn / markOut); the box itself never moves. One delegated listener,
// like the desktop's application-wide filter (support/controlSwap.hpp); programmatic
// changes fire no `change`, so the settings mirror calls setChecked() instead.

// The desktop's NO_CONTROL_SWAP_PROPERTY.
const NO_SWAP_ATTR = 'data-no-mark-dust';

// An unchecked box paints nothing, so with no cache the look is probed by ticking it for one
// synchronous style read — no event fires in between.
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

// `on` is the state just reached: a tick gathers, an untick scatters. Anything that
// cannot be dusted does nothing — the state itself has already changed.
function playCheckDust(el, on) {
  if (!el?.getBoundingClientRect || el.hasAttribute?.(NO_SWAP_ATTR)) return false;
  const ink = checkedInk(el);
  if (!ink) return false;
  const paint = { fill: ink, edge: ink };
  return on ? markIn(el, { paint }) : markOut(el, { paint });
}

// Set a checkbox from code and animate the change (core/controller.js writes through here).
export function setChecked(el, value) {
  if (!el) return;
  const on = !!value;
  if (el.checked === on) return;
  el.checked = on;
  playCheckDust(el, on);
}

// A check glyph written in and out of a span (the context menu's rows); the span is empty
// when off, so the arrival needs no extra veil.
export function swapCheckGlyph(el, html) {
  if (!el) return;
  const had = !!el.innerHTML;
  const has = !!html;
  if (had === has) { el.innerHTML = html; return; }
  if (has) { el.innerHTML = html; markIn(el); return; }
  markOut(el);
  el.innerHTML = html;
}

// One delegated listener for every checkbox or radio, including a click on its <label>.
export function installControlSwap(root = typeof document !== 'undefined' ? document : null) {
  if (!root?.addEventListener || root.__markSwapWired) return;
  root.__markSwapWired = true;
  root.addEventListener('change', (e) => {
    const el = e.target;
    if (!el || (el.type !== 'checkbox' && el.type !== 'radio')) return;
    playCheckDust(el, el.checked);
    if (el.type !== 'radio') return;
    // A radio takes the mark from its sibling, which loses its tick with no event of its own.
    if (el.name)
      for (const other of root.querySelectorAll(`input[type=radio][name="${CSS.escape(el.name)}"]`))
        if (other !== el && other.__markWasOn) { playCheckDust(other, false); other.__markWasOn = false; }
    el.__markWasOn = true;
  }, true);
}
