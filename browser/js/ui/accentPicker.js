import { ACCENTS, accentHex, normalizeHex } from '../core/accents.js';
import { icon } from './icons.js';
import { showMenu, hideMenu } from './dropdownMenu.js';

// How long the pointer must rest on a swatch row before it previews — see fillAccentMenu.
const PREVIEW_HOVER_MS = 280;

// The accent flood is a View Transition: it drops real :hover and fires synthetic
// enter/leave, so `:hover` lies while one plays. These read the truth instead.
const swapping = () => !!document.documentElement?.classList?.contains('theme-instant');
// The pointer's real position — a flood never fires a pointermove. One listener for
// every menu (a `:hover` check looped: restore fired after each flood, re-arming it).
let lastPt = null;
let tracking = false;
const trackPointer = () => {
  if (tracking) return;
  tracking = true;
  document.addEventListener('pointermove', (e) => { lastPt = { x: e.clientX, y: e.clientY }; }, true);
  // The pointer left the WINDOW (to the tab bar, another app): no pointermove follows, so
  // the last one would stand as a stale in-menu position and the preview would never
  // revert. A null relatedTarget is the document boundary.
  document.addEventListener('pointerout', (e) => { if (!e.relatedTarget) lastPt = null; }, true);
};
const pointerIn = (el) => {
  if (!lastPt) return false;
  const r = el.getBoundingClientRect();
  return lastPt.x >= r.left && lastPt.x <= r.right && lastPt.y >= r.top && lastPt.y <= r.bottom;
};

// Custom "main theme" dropdown: a native <select> can't paint a per-option colour
// swatch on every OS (notably macOS), so this does it explicitly. `value` is a preset
// key OR a custom #rrggbb — custom shows as "Custom" with its live swatch.
// Shared preset rows, used by the Visuals dropdown below AND the logo's right-click
// menu (ui/toolbar.js) — one list implementation, so the two can never drift; NO
// "Custom…" row in either (custom colours come only from the logo double-click).
// Every chip carries a ✓ that only the ACTIVE row shows (keyed off aria-selected;
// desktop parity: mainWindow.cpp paints the same tick into the current swatch).
// `preview` (optional): `{ on(key), off() }` — resting on a preset row paints it instantly
// so the page shows how it looks; leaving the list puts the committed accent back. Preview
// is a repaint only (accentController.previewAccent), never a persist; a click commits.
export function fillAccentMenu(menu, onPick, preview = null) {
  // Hover preview, hardened against the flood's own churn: it fires only after a rest AND
  // only when the key CHANGES, and the restore waits out the flood before checking that
  // the pointer has really left. Without both, the synthetic enter/leave ping-ponged
  // preview → restore → preview forever (user report).
  if (preview) trackPointer();
  let shownKey = null;        // the key currently previewed, or null = the committed accent
  let hoverTimer = null;
  let leaveTimer = null;
  const clearHover = () => { if (hoverTimer) { clearTimeout(hoverTimer); hoverTimer = null; } };
  const clearLeave = () => { if (leaveTimer) { clearTimeout(leaveTimer); leaveTimer = null; } };
  // A hovered row looks SELECTED — the accent style + the ✓ move onto it (user decision),
  // via aria-selected, an attribute, so it survives the flood's :hover drop. The committed
  // row's mark is snapshotted and put back on leave.
  let committedSel = null;
  const currentSel = () => {
    const r = menu.querySelector('.accent-dd-opt[aria-selected="true"]');
    return r ? r.dataset.key : null;
  };
  const setHovered = (li) => {
    if (!li) return;
    if (committedSel === null) committedSel = currentSel();
    markSelected(menu, li.dataset.key);
  };
  const restore = () => {
    clearHover();
    if (committedSel !== null) { markSelected(menu, committedSel); committedSel = null; }
    if (preview && shownKey !== null) { shownKey = null; preview.off(); }
  };
  const scheduleRestore = () => {
    clearHover();
    clearLeave();
    const settle = () => {
      if (swapping()) { leaveTimer = setTimeout(settle, 60); return; }   // wait out the flood
      leaveTimer = setTimeout(() => { if (!pointerIn(menu)) restore(); }, 90);
    };
    settle();
  };
  for (const a of ACCENTS) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    li.innerHTML =
      `<span class="accent-swatch" style="background:${a.hex}">` +
      `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
      `<span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => { clearHover(); clearLeave(); committedSel = null; shownKey = a.key; onPick(a.key); });
    if (preview) {
      li.addEventListener('pointerenter', () => {
        clearLeave();
        if (swapping()) return;   // synthetic enter from the flood — keep the current highlight
        setHovered(li);           // the hovered row takes the selected style + ✓
        if (shownKey === a.key) return;   // already showing this one
        clearHover();
        hoverTimer = setTimeout(() => {
          hoverTimer = null;
          if (shownKey === a.key) return;
          shownKey = a.key;
          preview.on(a.key);
        }, PREVIEW_HOVER_MS);
      });
      li.addEventListener('pointerleave', clearHover);
    }
    menu.appendChild(li);
  }
  // Leaving the whole list reverts — but only once the flood has settled and the pointer
  // has really gone (a synthetic mid-flood leave, or a hop to the next row, must not).
  if (preview) menu.addEventListener('pointerleave', scheduleRestore);
}

// Reflect the active accent on the rows (aria-selected drives the highlight AND the ✓).
// A non-preset value (custom hex, or null) simply selects nothing.
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

  fillAccentMenu(menu, (key) => choose(key), preview);

  // NOTE: no "Custom…" row — a custom colour is set ONLY from the header logo (double-click). The
  // dropdown just DISPLAYS the custom state in its trigger ("Custom" + the live swatch) below.
  const syncTrigger = () => {
    curSw.style.background = swatchOf(value);
    curName.textContent = labelOf(value);
    markSelected(menu, value);
  };
  // The open menu is portaled to <body>, so test it as well as the picker itself.
  const onDocClick = (e) => { if (!mount.contains(e.target) && !menu.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') { close(); trigger.focus(); } };
  const open = () => {
    showMenu(menu, trigger);   // viewport-placed, so a modal's scroll box can't clip it
    trigger.setAttribute('aria-expanded', 'true');
    document.addEventListener('pointerdown', onDocClick, true);
    document.addEventListener('keydown', onKey);
  };
  const close = () => {
    preview?.off();   // a menu dismissed mid-hover reverts to the committed accent
    hideMenu(menu);
    trigger.setAttribute('aria-expanded', 'false');
    document.removeEventListener('pointerdown', onDocClick, true);
    document.removeEventListener('keydown', onKey);
  };
  // onSelect commits BEFORE close: the real apply drops the preview snapshot, so close's
  // off() is a no-op and the page never flashes back to the old accent between the two.
  const choose = (key) => { value = key; syncTrigger(); onSelect?.(key); close(); };

  trigger.addEventListener('click', (e) => { e.preventDefault(); menu.hidden ? open() : close(); });
  syncTrigger();
  return { set: (k) => { value = k; syncTrigger(); } };
}
