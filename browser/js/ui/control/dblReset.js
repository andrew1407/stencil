// Double-click a selector or a checkbox and it goes back to its default, applied as a pick
// (desktop twin: support/control/dblReset.hpp). The default is the DEFAULTS table, else
// `data-default`, else the markup's own `selected` / `checked`, else the first option.

import { createEditorState } from '../../core/editorState.js';
import { DEFAULT_PERIOD } from '../../core/project/meta/projectPeriods.js';
import { defaultSettings as llmDefaults } from '../../llm/settings.js';
import { DEFAULT_DRAWING_ANIMATIONS, DEFAULT_MODAL_BACKDROP, DEFAULT_MOTION_MODE } from '../motion/motionPrefs.js';
import { DEFAULT_NOTIFY_CHANNEL } from '../../core/settings/notifyChannel.js';

// The controls whose default the markup cannot say: read when the double-click lands, so a
// window rendered later still finds its own.
const editor = (key) => () => createEditorState()[key];
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
});
// Menu rows that toggle in place: their state is the toolbar check they mirror.
const MIRRORS = Object.freeze({ 'ctx-show-points': 'show-points', 'ctx-show-lines': 'show-lines' });

// Rows whose double-click opens something of their own keep it.
const SKIP = '.project-row, .connect-row, .lines-row, .coordinates-table, [data-no-reset]';

/** The control a double-click on `target` resets: a select behind its custom trigger, a checkbox, or a mirrored menu row. */
export const resetTarget = (target) => {
  if (!(target instanceof Element) || target.closest(SKIP)) return null;
  const row = target.closest(Object.keys(MIRRORS).map((id) => `#${id}`).join(', '));
  if (row) return row;
  const trigger = target.closest('.accent-dd-trigger');
  if (trigger) return trigger.closest('.cs-dd')?.querySelector('select') ?? null;
  const select = target.closest('select');
  if (select) return select;
  if (target instanceof HTMLInputElement && target.type === 'checkbox') return target;
  const label = target.closest('label, .pill-toggle');
  const box = label?.querySelector('input[type="checkbox"]');
  return box ?? null;
};

/** What `el` resets to. */
export const defaultOf = (el) => {
  const stated = DEFAULTS[el.id] ? String(DEFAULTS[el.id]()) : el.dataset.default;
  if (el.type === 'checkbox') return stated !== undefined ? stated === 'true' : el.defaultChecked;
  if (stated !== undefined) return stated;
  const opt = [...el.options].find((o) => o.defaultSelected) ?? el.options[0];
  return opt ? opt.value : el.value;
};

/** Puts `el` back to its default through its own change path; false when it already was. */
export const resetControl = (el) => {
  if (MIRRORS[el.id]) {
    const box = el.ownerDocument.getElementById(MIRRORS[el.id]);
    if (!box || box.checked === defaultOf(box)) return false;
    el.click();   // the row's own toggle, which also re-draws its tick
    return true;
  }
  if (el.disabled) return false;
  const want = defaultOf(el);
  if (el.type === 'checkbox') {
    if (el.checked === want) return false;
    el.checked = want;
  } else {
    if (el.value === want || ![...el.options].some((o) => o.value === want)) return false;
    el.value = want;
  }
  el.dispatchEvent(new Event('input', { bubbles: true }));
  el.dispatchEvent(new Event('change', { bubbles: true }));
  return true;
};

/** One listener for the whole document, in the capture phase so a row gesture cannot eat it. */
export const installDblReset = (root = document) => {
  root.addEventListener('dblclick', (e) => {
    const el = resetTarget(e.target);
    if (el && resetControl(el)) e.stopPropagation();
  }, true);
};
