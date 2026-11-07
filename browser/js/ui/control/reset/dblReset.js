// Double-click a selector, a checkbox or a colour field and it goes back to its default, applied
// as a pick (desktop twin: support/control/dblReset.hpp). The default is the DEFAULTS table, else
// `data-default`, else the markup's own `selected` / `checked`, else the first option.

import { normalizeHex } from '../../../core/settings/accents.js';
import { dragsStarted } from '../../drag/iconDrag.js';
import { openColorPicker } from '../../bindings/controls/colorTrial.js';
import { isTouchLike } from '../../../utils.js';
import { DEFAULTS, MIRRORS } from './defaults.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

const MIRROR_SELECTOR = Object.keys(MIRRORS).map((id) => `#${id}`).join(', ');

// Rows whose double-click opens something of their own keep it.
const SKIP = '.project-row, .connect-row, .lines-row, .coordinates-table, [data-no-reset]';

// A colour field has no markup default worth returning to, so only a stated one opts it in.
const hasStatedDefault = (el) => !!DEFAULTS[el.id] || el.dataset.default !== undefined;

// A settings row's title stands for its control (desktop: the caption's CAPTION_FIELD_PROPERTY).
const ROW_TITLE = '.vs-row > label:first-child';
const rowControl = (title) => {
  const el = title.closest('.vs-row')?.querySelector('.vs-ctrl select, .vs-ctrl input');
  if (!el) return null;
  return el.tagName === 'SELECT' || el.type === 'checkbox' || hasStatedDefault(el) ? el : null;
};

/** The control a double-click on `target` resets: a select behind its custom trigger, a checkbox, a colour field with a stated default, or a mirrored menu row. */
export const resetTarget = (target) => {
  if (!(target instanceof Element) || target.closest(SKIP)) return null;
  const title = target.closest(ROW_TITLE);
  if (title) return rowControl(title);
  const row = target.closest(MIRROR_SELECTOR);
  if (row) return row;
  const trigger = target.closest('.accent-dd-trigger');
  if (trigger) return trigger.closest('.cs-dd')?.querySelector('select') ?? null;
  const select = target.closest('select');
  if (select) return select;
  if (target instanceof HTMLInputElement && target.type === 'checkbox') return target;
  if (target instanceof HTMLInputElement && target.type === 'color' && hasStatedDefault(target)) return target;
  const label = target.closest('label, .pill-toggle');
  const box = label?.querySelector('input[type="checkbox"]');
  return box ?? null;
};

const isField = (el) => typeof HTMLInputElement !== 'undefined' && el instanceof HTMLInputElement
  && (el.type === 'number' || el.type === 'text');

/** What a logo dropped on `target` resets: what a double-click does, plus a number or formula field with a stated default. */
export const dropResetTarget = (target) => {
  if (typeof Element === 'undefined' || !(target instanceof Element)) return null;
  const el = resetTarget(target);
  if (el) return el;
  return isField(target) && DEFAULTS[target.id] && !target.closest(SKIP) ? target : null;
};

/** What `el` resets to. */
export const defaultOf = (el) => {
  const stated = DEFAULTS[el.id] ? String(DEFAULTS[el.id](el)) : el.dataset.default;
  if (el.type === 'checkbox') return stated !== undefined ? stated === 'true' : el.defaultChecked;
  if (el.type === 'color') return normalizeHex(stated ?? el.defaultValue) ?? el.value;
  if (stated !== undefined) return stated;
  const opt = [...el.options].find((o) => o.defaultSelected) ?? el.options[0];
  return opt ? opt.value : el.value;
};

// How each kind of control takes its default: false when it already holds it, or cannot.
const TAKE = Object.freeze({
  checkbox: (el, want) => {
    if (el.checked === want) return false;
    el.checked = want;
    return true;
  },
  color: (el, want) => {
    if (normalizeHex(el.value) === want) return false;
    el.value = want;
    return true;
  },
  number: (el, want) => {
    if (Number(el.value) === Number(want)) return false;
    el.value = String(want);
    return true;
  },
  range: (el, want) => TAKE.number(el, want),
  text: (el, want) => {
    if (el.value === want) return false;
    el.value = want;
    return true;
  },
});
const takeOption = (el, want) => {
  if (el.value === want || ![...el.options].some((o) => o.value === want)) return false;
  el.value = want;
  return true;
};

/** Puts `el` back to its default through its own change path; false when it already was. */
export const resetControl = (el) => {
  if (MIRRORS[el.id]) {
    const box = el.ownerDocument.getElementById(MIRRORS[el.id]);
    if (!box || box.checked === defaultOf(box)) return false;
    el.click();   // the row's own toggle, which also re-draws its tick
    return true;
  }
  if (el.disabled || !(TAKE[el.type] ?? takeOption)(el, defaultOf(el))) return false;
  el.dispatchEvent(new Event('input', { bubbles: true }));
  el.dispatchEvent(new Event('change', { bubbles: true }));
  return true;
};

const { doubleClickMs, doubleTapMs } = constants.POPOVER;

// The native picker opens on the first click and would swallow the second, so a colour field with
// a default opens once the double-click window passes; a click inside it is the double, and resets.
const deferColorPicks = (root) => {
  let pending = null;
  const cancel = () => { if (pending) clearTimeout(pending.timer); pending = null; };
  root.addEventListener('click', (e) => {
    if (e.detail === 0 || !(e.target instanceof HTMLInputElement) || e.target.type !== 'color') return;
    const el = resetTarget(e.target);
    if (el !== e.target) return;
    e.preventDefault();
    if (pending?.el === el) { cancel(); resetControl(el); return; }
    cancel();
    const wait = e.pointerType === 'touch' ? doubleTapMs : doubleClickMs;
    const drags = dragsStarted();   // a second press that dragged the swatch away picks nothing
    pending = { el, timer: setTimeout(() => { pending = null; if (dragsStarted() === drags) openColorPicker(el, null); }, wait) };
  }, true);
};

/** A button that opens a colour picker (desktop wireColorChip): the open waits out the double-click window, a second click inside it resets. */
export const wireColorButton = (btn, { open, reset }) => {
  let timer = null;
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    if (timer) { clearTimeout(timer); timer = null; reset(); return; }
    if (e.detail === 0) { open(); return; }   // a keyboard press has no second click to wait for
    const drags = dragsStarted();
    timer = setTimeout(() => { timer = null; if (dragsStarted() === drags) open(); },
      e.pointerType === 'touch' ? doubleTapMs : doubleClickMs);
  });
};

// An open list would take the second click, so a selector's list opens once the double-click window passes
// (desktop DblResetFilter); a second click inside it is the double, which the dblclick listener resets.
const deferSelectOpens = (root) => {
  let pending = null;
  root.addEventListener('click', (e) => {
    const trigger = e.target instanceof Element ? e.target.closest('.accent-dd-trigger') : null;
    if (!trigger || e.detail === 0 || trigger.getAttribute('aria-expanded') === 'true') return;
    const el = resetTarget(e.target);
    if (!el || el.tagName !== 'SELECT' || el.disabled) return;
    e.stopPropagation();
    e.preventDefault();
    const again = pending?.trigger === trigger;
    if (pending) clearTimeout(pending.timer);
    pending = null;
    if (again) return;
    const wait = e.pointerType === 'touch' ? doubleTapMs : doubleClickMs;
    pending = { trigger, timer: setTimeout(() => { pending = null; trigger.click(); }, wait) };   // detail 0: opens
  }, true);
};

// Touch fires no dblclick (never on iOS, not always in Chromium): two taps on one control inside the double-tap
// window are the double (user report). A colour field counts its own second click (deferColorPicks).
const doubleTaps = (root) => {
  let last = null;
  root.addEventListener('click', (e) => {
    if (e.detail === 0 || !(e.pointerType === 'touch' || (e.pointerType === undefined && isTouchLike()))) return;
    const el = resetTarget(e.target);
    if (!el || (el instanceof HTMLInputElement && el.type === 'color')) { last = null; return; }
    const at = performance.now();
    const again = last?.el === el && at - last.at <= doubleTapMs;
    last = again ? null : { el, at };
    if (again && resetControl(el)) e.stopPropagation();
  }, true);
};

/** One listener for the whole document, in the capture phase so a row gesture cannot eat it. */
export const installDblReset = (root = document) => {
  root.addEventListener('dblclick', (e) => {
    const el = resetTarget(e.target);
    if (el && resetControl(el)) e.stopPropagation();
  }, true);
  doubleTaps(root);
  deferColorPicks(root);
  deferSelectOpens(root);
};
