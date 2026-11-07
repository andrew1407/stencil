// Double-click a selector or a checkbox and it goes back to its default, as the editor's own
// controls do (browser js/ui/control/reset/dblReset.js). The Options page still saves on Save; the
// popup's filters apply at once through their change handlers.
import { DEFAULT_PAGE } from '../prefs/settings.js';
import constants from '../tip/constants.json' with { type: 'json' };

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

// No stated field defaults here, so a drop resets just what a double-click does.
export const dropResetTarget = (target) => resetTarget(target);

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

const { doubleClickMs, doubleTapMs } = constants.POPOVER;

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

export const installDblReset = (root = document) => {
  root.addEventListener('dblclick', (e) => {
    const el = resetTarget(e.target);
    if (el && resetControl(el)) e.stopPropagation();
  }, true);
  deferSelectOpens(root);
};

// A button that opens a colour picker: the open waits out the double-click window, a second click resets.
export const wireColorButton = (btn, { open, reset }) => {
  let timer = null;
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    if (timer) { clearTimeout(timer); timer = null; reset(); return; }
    if (e.detail === 0) { open(); return; }   // a keyboard press has no second click to wait for
    timer = setTimeout(() => { timer = null; open(); }, e.pointerType === 'touch' ? doubleTapMs : doubleClickMs);
  });
};
