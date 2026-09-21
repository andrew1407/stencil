import { ACCENTS, accentHex, normalizeHex, onAccentInk } from '../../core/accents.js';
import { icon } from '../icons.js';
import { showMenu, hideMenu } from '../control/dropdownMenu.js';
import { attachMenuScrollbar } from '../control/menuScrollbar.js';

const PREVIEW_HOVER_MS = 280;

// The accent flood is a View Transition: it drops real :hover and fires synthetic
// enter/leave, so `:hover` lies while one plays.
const swapping = () => !!document.documentElement?.classList?.contains('theme-instant');
// The pointer's real position — a flood never fires a pointermove.
let lastX = NaN, lastY = NaN;
let tracking = false;
const trackPointer = () => {
  if (tracking) return;
  tracking = true;
  document.addEventListener('pointermove', (e) => { lastX = e.clientX; lastY = e.clientY; }, { capture: true, passive: true });
// The pointer left the window: no pointermove follows (null relatedTarget = document boundary).
  document.addEventListener('pointerout', (e) => { if (!e.relatedTarget) lastX = NaN; }, { capture: true, passive: true });
};
const pointerIn = (el) => {
  if (Number.isNaN(lastX)) return false;
  const r = el.getBoundingClientRect();
  return lastX >= r.left && lastX <= r.right && lastY >= r.top && lastY <= r.bottom;
};

// A native <select> cannot paint per-option swatches on every OS. `value` is a preset key or a
// custom #rrggbb; `preview` = { on(key), off() } repaints only — a click commits.
export function fillAccentMenu(menu, onPick, preview = null) {
// The preview fires only after a rest and when the key changes; the restore waits out
// the flood before checking the pointer has really left.
  if (preview) trackPointer();
  let shownKey = null;
  let hoverTimer = null;
  let leaveTimer = null;
  const clearHover = () => { if (hoverTimer) { clearTimeout(hoverTimer); hoverTimer = null; } };
  const clearLeave = () => { if (leaveTimer) { clearTimeout(leaveTimer); leaveTimer = null; } };
// A hovered row takes the selected style via aria-selected (survives the flood's :hover
// drop); the committed row's mark is put back on leave.
  let committedSel = null;
  let committing = false;
  const currentSel = () => {
    const r = menu.querySelector('.accent-dd-opt[aria-selected="true"]');
    return r ? r.dataset.key : null;
  };
  const setHovered = (li) => {
    if (!li) return;
    if (committedSel === null) committedSel = currentSel();
    markSelected(menu, li.dataset.key);
    latchHover(li);
  };
// Run once the flood a call is about to start has finished.
  const afterSwap = (fn) => {
    const poll = () => { if (swapping()) { setTimeout(poll, 60); return; } fn(); };
    setTimeout(poll, 60);
  };
// `dd-preview-hold` freezes the rows' hover replays while a preview shows (themeSwap.css).
  const holdReplays = (on) => {
    menu.classList?.[on ? 'add' : 'remove']('dd-preview-hold');
    holdCursor(on);
  };
// The flood's snapshot steals the row's hit test; hold the hand cursor only while it is up.
  const holdCursor = (on) => {
    const root = document.documentElement;
    root?.classList?.[on ? 'add' : 'remove']('dd-preview-cursor');
    if (on) afterSwap(() => root?.classList?.remove('dd-preview-cursor'));
  };
// The row's :hover treatment latched as a class, since the flood drops real :hover.
  const latchHover = (li) => {
    for (const r of menu.children) r.classList?.toggle('dd-hover', r === li);
  };
  const restore = () => {
    clearHover();
    if (committedSel !== null) { markSelected(menu, committedSel); committedSel = null; }
    if (preview && shownKey !== null) { shownKey = null; preview.off(); }
// A pick closes the list over its own flood: the commit's afterSwap releases the hold.
    if (committing) return;
    latchHover(null);
    holdReplays(false);
  };
  const scheduleRestore = () => {
    clearHover();
    clearLeave();
    const settle = () => {
      if (swapping()) { leaveTimer = setTimeout(settle, 60); return; }
      leaveTimer = setTimeout(() => { if (!pointerIn(menu)) restore(); }, 90);
    };
    settle();
  };
  const pick = (key) => {
    clearHover(); clearLeave();
// The commit floods too: hold the replays until it settles.
    committing = true;
    holdCursor(false);
    afterSwap(() => { committing = false; holdReplays(false); latchHover(null); });
    committedSel = null; shownKey = key; onPick(key);
  };
// A view transition's snapshot owns the hit test while it plays, so a press mid-wipe answers
// <html>: nothing moves in a swap, so resolve it against the rows' boxes, on the window, in capture.
  const rowAt = (x, y) => {
    if (menu.hidden) return null;
    for (const li of menu.children) {
      if (!li.classList?.contains('accent-dd-opt') || li.style.display === 'none') continue;
      const r = li.getBoundingClientRect();
      if (r.width && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return li;
    }
    return null;
  };
  const onStolenPress = (e) => {
    if (menu.hidden || menu.contains(e.target)) return;
    const li = rowAt(e.clientX, e.clientY);
    if (!li) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    pick(li.dataset.key);
  };
  if (typeof window !== 'undefined' && window.addEventListener)
    window.addEventListener('pointerdown', onStolenPress, true);

  for (const a of ACCENTS) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    li.innerHTML =
      `<span class="accent-swatch" style="background:${a.hex};color:${onAccentInk(a.hex)}">` +
      `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
      `<span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => pick(a.key));
    if (preview) {
      li.addEventListener('pointerenter', () => {
        clearLeave();
// Synthetic enters (the flood's, or a list closing over its commit) must not start a
// preview nothing reverts.
        if (swapping() || committing) return;
// A real hop lifts the hold so the row plays its own hover once.
        if (!li.classList?.contains?.('dd-hover')) holdReplays(false);
        setHovered(li);
        if (shownKey === a.key) return;
        clearHover();
        hoverTimer = setTimeout(() => {
          hoverTimer = null;
          if (shownKey === a.key) return;
          shownKey = a.key;
          holdReplays(true);
          preview.on(a.key);
        }, PREVIEW_HOVER_MS);
      });
      li.addEventListener('pointerleave', clearHover);
    }
    menu.appendChild(li);
  }
// Leaving the list reverts once the flood has settled and the pointer has really gone.
  if (preview) menu.addEventListener('pointerleave', scheduleRestore);
// The owner calls this on dismissal so no latch or hold outlives the list.
  return restore;
}

// aria-selected drives the highlight and the ✓; a non-preset value selects nothing.
export function markSelected(menu, value) {
  for (const li of menu.children)
    li.setAttribute('aria-selected', li.dataset.key === value ? 'true' : 'false');
}

export function buildAccentPicker(mount, { current, onSelect, preview = null }) {
  const isPreset = (k) => ACCENTS.some((a) => a.key === k);
  const labelOf = (k) => (isPreset(k) ? ACCENTS.find((a) => a.key === k).label : 'Custom');
  const swatchOf = (k) => (isPreset(k) ? accentHex(k) : normalizeHex(k) || accentHex('violet'));
  let value = current;

  mount.classList.add('accent-dd');
  mount.innerHTML = `
    <button type="button" class="accent-dd-trigger" aria-haspopup="listbox" aria-expanded="false">
      <span class="accent-swatch js-cur-sw"></span>
      <span class="accent-dd-name js-cur-name"></span>
      <span class="accent-dd-caret" aria-hidden="true">${icon('chevron-down', { size: 13 })}</span>
    </button>
    <ul class="accent-dd-menu" role="listbox" hidden></ul>`;
  const trigger = mount.querySelector('.accent-dd-trigger');
  const menu = mount.querySelector('.accent-dd-menu');
  const curSw = mount.querySelector('.js-cur-sw');
  const curName = mount.querySelector('.js-cur-name');

  const resetHover = fillAccentMenu(menu, (key) => choose(key), preview);

// No "Custom…" row: a custom colour is set only from the header logo (double-click).
  const syncTrigger = () => {
    curSw.style.background = swatchOf(value);
    curSw.style.color = onAccentInk(swatchOf(value));
    curName.textContent = labelOf(value);
    markSelected(menu, value);
  };
  const onDocClick = (e) => { if (!mount.contains(e.target) && !menu.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') { close(); trigger.focus(); } };
  const open = () => {
    showMenu(menu, trigger);
    attachMenuScrollbar(menu);   // only a list too tall for its cap draws a thumb
    trigger.setAttribute('aria-expanded', 'true');
    document.addEventListener('pointerdown', onDocClick, true);
    document.addEventListener('keydown', onKey);
  };
  const close = () => {
    resetHover();     // a menu dismissed mid-hover reverts to the committed accent, and
    preview?.off();   // drops the row's latched hover with it
    hideMenu(menu);
    trigger.setAttribute('aria-expanded', 'false');
    document.removeEventListener('pointerdown', onDocClick, true);
    document.removeEventListener('keydown', onKey);
  };
// onSelect commits before close, so close's off() is a no-op and nothing flashes back.
  const choose = (key) => { value = key; syncTrigger(); onSelect?.(key); close(); };

  trigger.addEventListener('click', (e) => { e.preventDefault(); menu.hidden ? open() : close(); });
  syncTrigger();
  return { set: (k) => { value = k; syncTrigger(); } };
}
