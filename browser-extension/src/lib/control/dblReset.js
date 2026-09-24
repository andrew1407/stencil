// Double-click a selector or a checkbox and it goes back to its default, as the editor's own
// controls do (browser js/ui/control/dblReset.js). The Options page still saves on Save; the
// popup's filters apply at once through their change handlers.
import { DEFAULT_PAGE } from '../prefs/settings.js';

// getSettings() and prefs.js state these (prefs.js keeps its own inside its IIFE); the LLM
// fields carry theirs as data-default, stamped by options/llm.js.
const DEFAULTS = Object.freeze({
  page: () => DEFAULT_PAGE, markOpened: () => true, openedFirst: () => true,
  exposeWindowStencil: () => false, editorPageApi: () => true, 'hl-mode': () => 'theme',
  appearance: () => 'system', motion: () => 'particles',
  'pin-search-mode': () => 'common', 'pin-store': () => 'all', 'pin-site': () => 'all',
  'f-mark-opened': () => true, 'f-opened-first': () => true, 'f-show-pinned': () => true, 'f-hover-hl': () => false,
});

// Rows whose double-click opens something of their own keep it.
const SKIP = '.row, .src-row, .ed-row, .pin-row, [data-no-reset]';

export const resetTarget = (target) => {
  if (!(target instanceof Element) || target.closest(SKIP)) return null;
  const trigger = target.closest('.accent-dd-trigger');
  if (trigger) return trigger.closest('.cs-dd')?.querySelector('select') ?? null;
  const select = target.closest('select');
  if (select) return select;
  if (target instanceof HTMLInputElement && target.type === 'checkbox') return target;
  return target.closest('label')?.querySelector('input[type="checkbox"]') ?? null;
};

export const defaultOf = (el) => {
  const stated = DEFAULTS[el.id] ? String(DEFAULTS[el.id]()) : el.dataset.default;
  if (el.type === 'checkbox') return stated !== undefined ? stated === 'true' : el.defaultChecked;
  if (stated !== undefined) return stated;
  const opt = [...el.options].find((o) => o.defaultSelected) ?? el.options[0];
  return opt ? opt.value : el.value;
};

export const resetControl = (el) => {
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

export const installDblReset = (root = document) => {
  root.addEventListener('dblclick', (e) => {
    const el = resetTarget(e.target);
    if (el && resetControl(el)) e.stopPropagation();
  }, true);
};
