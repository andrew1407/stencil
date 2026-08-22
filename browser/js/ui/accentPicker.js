import { ACCENTS, accentHex, normalizeHex } from '../core/accents.js';
import { icon } from './icons.js';
import { showMenu, hideMenu } from './dropdownMenu.js';

// Custom "main theme" dropdown: a native <select> can't paint a per-option colour
// swatch on every OS (notably macOS), so this does it explicitly. `value` is a preset
// key OR a custom #rrggbb — custom shows as "Custom" with its live swatch.
// Shared preset rows, used by the Visuals dropdown below AND the logo's right-click
// menu (ui/toolbar.js) — one list implementation, so the two can never drift; NO
// "Custom…" row in either (custom colours come only from the logo double-click).
// Every chip carries a ✓ that only the ACTIVE row shows (keyed off aria-selected;
// desktop parity: mainWindow.cpp paints the same tick into the current swatch).
export function fillAccentMenu(menu, onPick) {
  for (const a of ACCENTS) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    li.innerHTML =
      `<span class="accent-swatch" style="background:${a.hex}">` +
      `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
      `<span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => onPick(a.key));
    menu.appendChild(li);
  }
}

// Reflect the active accent on the rows (aria-selected drives the highlight AND the ✓).
// A non-preset value (custom hex, or null) simply selects nothing.
export function markSelected(menu, value) {
  for (const li of menu.children)
    li.setAttribute('aria-selected', li.dataset.key === value ? 'true' : 'false');
}

export function buildAccentPicker(mount, { current, onSelect }) {
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

  fillAccentMenu(menu, (key) => choose(key));

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
    hideMenu(menu);
    trigger.setAttribute('aria-expanded', 'false');
    document.removeEventListener('pointerdown', onDocClick, true);
    document.removeEventListener('keydown', onKey);
  };
  const choose = (key) => { value = key; syncTrigger(); close(); onSelect?.(key); };

  trigger.addEventListener('click', (e) => { e.preventDefault(); menu.hidden ? open() : close(); });
  syncTrigger();
  return { set: (k) => { value = k; syncTrigger(); } };
}
