import { setVal, setRadioGroup } from '../utils.js';
import { icon } from './icons.js';
import { setChecked, swapCheckGlyph } from './control/controlSwap.js';
import { revealControls } from './motion.js';

// ── How a setting shows on screen ────────────────────────────────
// SettingsController owns the model and WHEN a setting changes; these own the elements it
// is reflected in, so js/core carries no element ids. Every painter tolerates a missing
// element — a setting can be written before (or without) the control that shows it.
const el = (id) => document.getElementById(id);

// One bound element per {id, kind}. 'valueSkipFocus' leaves the element alone while the
// user is typing in it (the ctx-* number twins); 'radio' addresses a GROUP by input[name].
const PAINTERS = Object.freeze({
  value: (node, value) => { node.value = value; },
  valueSkipFocus: (node, value) => { if (document.activeElement !== node) node.value = value; },
  // Both marks come and go as sand (ui/controlSwap.js): a programmatic change — Alt+P,
  // the context-menu twin, a restored project — animates exactly as a click does.
  checked: (node, value) => setChecked(node, value),
  checkIcon: (node, value) => swapCheckGlyph(node, value ? icon('check', { size: 14 }) : ''),
});
export const applyMirror = ({ id, kind }, value) => {
  if (kind === 'radio') { setRadioGroup(id, value); return; }
  const node = el(id);
  if (!node) return;
  PAINTERS[kind]?.(node, value);
};

// The tint picker and the context menu's tint row follow the 'custom' filter.
export const paintTintControls = (isCustom) => {
  const picker = el('filter-color');
  if (picker) picker.style.display = isCustom ? 'inline-block' : 'none';
  el('ctx-tint-row')?.classList.toggle('ctx-tint-visible', isCustom);
};

// The W/H boxes a custom page needs form out of motes, and come apart into them again
// when a named format takes over.
export const paintCustomSizeGroup = (isCustom) => revealControls(el('custom-size-group'), isCustom);

// Both formula input groups plus the toolbar pill. The pill's `.on` is set deterministically:
// the CSS :has() selector does not restyle reliably on a programmatic state change.
export const paintFormulaToggle = (checked) => {
  revealControls(el('formula-inputs'), checked);
  revealControls(el('ctx-formula-inputs'), checked, 'block');
  setChecked(el('ctx-allow-formulas'), checked);
  const mainCb = el('allow-formulas');
  if (mainCb) {
    setChecked(mainCb, checked);
    mainCb.closest('.pill-toggle')?.classList.toggle('on', checked);
  }
};

export const paintFormulaError = (hasError) => {
  const inline = el('formula-error');
  const ctx = el('ctx-formula-error');
  if (inline) inline.style.display = hasError ? 'inline' : 'none';
  if (ctx) ctx.style.display = hasError ? 'block' : 'none';
};

/** Run `fn` over each of these controls that is actually mounted. */
export const forEachControl = (ids, fn) => {
  for (const id of ids) {
    const node = el(id);
    if (node) fn(node);
  }
};

/** A control's trimmed text, '' when it is not mounted. */
export const readControl = (id) => (el(id)?.value || '').trim();

export const paintTooltipOption = (id, on) => {
  const box = el(id);
  if (box) box.checked = !!on;
};

export const paintMotionMode = (mode) => setVal('vs-motion-mode', mode);
export const paintMotionDrawing = (on) => setChecked(el('vs-draw-anim'), !!on);
export const paintMotionBackdrop = (on) => setChecked(el('vs-modal-backdrop'), !!on);
