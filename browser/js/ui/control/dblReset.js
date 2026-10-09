// Double-click a selector, a checkbox or a colour field and it goes back to its default, applied
// as a pick (desktop twin: support/control/dblReset.hpp). The default is the DEFAULTS table, else
// `data-default`, else the markup's own `selected` / `checked`, else the first option.

import { createEditorState } from '../../core/editorState.js';
import { DEFAULT_PERIOD } from '../../core/project/meta/projectPeriods.js';
import { defaultSettings as llmDefaults } from '../../llm/settings.js';
import { DEFAULT_DRAWING_ANIMATIONS, DEFAULT_MODAL_BACKDROP, DEFAULT_MOTION_MODE } from '../motion/motionPrefs.js';
import { DEFAULT_NOTIFY_CHANNEL } from '../../core/settings/notifyChannel.js';
import { normalizeHex } from '../../core/settings/accents.js';
import { dragsStarted } from '../drag/iconDrag.js';
import { openColorPicker } from '../bindings/controls/colorTrial.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

// The controls whose default the markup cannot say: read when the double-click lands, so a
// window rendered later still finds its own.
const editor = (key) => () => createEditorState()[key];
// A line's own colours reset to the ones a new line would take: the toolbar pickers'. The toolbar's
// point colour itself takes the toolbar's line colour; an area's fill the Visuals row's.
const fieldOr = (id, key) => (el) => el.ownerDocument?.getElementById(id)?.value || createEditorState()[key];
const toolbarColor = fieldOr('line-color', 'color');
const DEFAULTS = Object.freeze({
  'page-size': editor('pageSize'), 'unit-select': editor('unit'), 'image-filter': editor('imageFilter'),
  'line-style': editor('style'), 'compare-mode': editor('compareMode'), 'show-points': editor('showPoints'),
  'show-lines': editor('showLines'), 'allow-formulas': editor('allowFormulas'),
  'vs-appearance': () => 'system', 'vs-style': editor('style'), 'vs-motion-mode': () => DEFAULT_MOTION_MODE,
  'vs-draw-anim': () => DEFAULT_DRAWING_ANIMATIONS, 'vs-modal-backdrop': () => DEFAULT_MODAL_BACKDROP,
  'vs-notify-channel': () => DEFAULT_NOTIFY_CHANNEL,
  'chat-provider': () => llmDefaults().provider, 'chat-save-chats': () => llmDefaults().saveChats,
  // openImage/modal.js onOpen and cropRows.js state these as literals.
  'open-image-incognito': () => false, 'open-image-rename': () => false, 'open-image-keep': () => true,
  'open-image-crop-toggle': () => false, 'open-image-crop-size': () => 'page',
  'projects-filter': () => 'all', 'projects-sort': () => 'name', 'projects-search-mode': () => 'common',
  'connect-autoconnect': () => true, 'connect-sync': () => true, 'connect-filter': () => 'all',
  'expiration-period': () => DEFAULT_PERIOD, 'expiration-auto': () => true, 'expiration-keep': () => false,
  'sel-style': () => 'solid', 'fs-sel-style': () => 'solid', 'open-in-incognito': () => false,
  'ctx-tt-enabled': editor('tooltipEnabled'), 'ctx-tt-page': editor('tooltipShowPage'),
  'ctx-tt-screen': editor('tooltipShowScreen'), 'ctx-tt-coords': editor('tooltipShowCoords'),
  'ctx-allow-formulas': editor('allowFormulas'),
  'line-color': editor('color'), 'sel-color': toolbarColor, 'fs-sel-color': toolbarColor,
  'point-color': toolbarColor, 'sel-point-color': fieldOr('point-color', 'color'),
  'fs-sel-point-color': fieldOr('point-color', 'color'),
  'sel-fill': fieldOr('vs-fill', 'defaultFillColor'), 'fs-sel-fill': fieldOr('vs-fill', 'defaultFillColor'),
  'filter-color': editor('filterColor'), 'ctx-tint-color': editor('filterColor'),
  'vs-line-color': editor('color'), 'vs-fill': editor('defaultFillColor'), 'vs-sel-glow': editor('selGlowColor'),
  'vs-hover-ring': editor('hoverRingColor'), 'vs-focus-ring': editor('focusRingColor'),
  'blank-image-color': () => '#ffffff',
  // Reached by a logo drop only: a double-click in a field selects its text.
  'line-thickness': editor('thickness'), 'point-size': editor('pointSize'),
  'ctx-thickness': editor('thickness'), 'ctx-point-size': editor('pointSize'),
  'formula-x': editor('formulaX'), 'formula-y': editor('formulaY'),
  'ctx-formula-x': editor('formulaX'), 'ctx-formula-y': editor('formulaY'),
});
// Menu rows that toggle in place: their state is the toolbar check they mirror.
const MIRRORS = Object.freeze({ 'ctx-show-points': 'show-points', 'ctx-show-lines': 'show-lines' });
const MIRROR_SELECTOR = Object.keys(MIRRORS).map((id) => `#${id}`).join(', ');

// Rows whose double-click opens something of their own keep it.
const SKIP = '.project-row, .connect-row, .lines-row, .coordinates-table, [data-no-reset]';

// A colour field has no markup default worth returning to, so only a stated one opts it in.
const hasStatedDefault = (el) => !!DEFAULTS[el.id] || el.dataset.default !== undefined;

/** The control a double-click on `target` resets: a select behind its custom trigger, a checkbox, a colour field with a stated default, or a mirrored menu row. */
export const resetTarget = (target) => {
  if (!(target instanceof Element) || target.closest(SKIP)) return null;
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

/** One listener for the whole document, in the capture phase so a row gesture cannot eat it. */
export const installDblReset = (root = document) => {
  root.addEventListener('dblclick', (e) => {
    const el = resetTarget(e.target);
    if (el && resetControl(el)) e.stopPropagation();
  }, true);
  deferColorPicks(root);
};
