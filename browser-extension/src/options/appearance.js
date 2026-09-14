// Accent, theme and interface animation live in localStorage via lib/accent.js, not the saved
// settings, so they apply instantly and flash-free across the extension's pages — no Save.
import { surfaceIn, surfaceOut } from '../lib/motion.js';
import { icon } from '../lib/icons.js';
import { menuDustPoint } from '../lib/dropdownMenu.js';
import { wireLogoAccent } from '../lib/logoAccent.js';

const accent = window.StencilAccent;
if (accent) {
  // A native <select> can't paint per-option swatches on every OS (e.g. macOS).
  const mount = document.getElementById('accent');
  const meta = (key) => accent.list.find((a) => a.key === key) || accent.list[0];
  let value = accent.get();

  mount.innerHTML =
    '<button type="button" class="accent-dd-trigger" aria-haspopup="listbox" aria-expanded="false">' +
    '<span class="accent-swatch js-cur-sw"></span><span class="accent-dd-name js-cur-name"></span>' +
    '<span class="accent-dd-caret" aria-hidden="true">' + icon('chevron-down', { size: 13 }) + '</span></button>' +
    '<ul class="accent-dd-menu" role="listbox" hidden></ul>';
  const trigger = mount.querySelector('.accent-dd-trigger');
  const menu = mount.querySelector('.accent-dd-menu');
  const curSw = mount.querySelector('.js-cur-sw');
  const curName = mount.querySelector('.js-cur-name');

  for (const a of accent.list) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    // Every chip carries the ✓; only the selected row reveals it (lib/theme/select.css).
    li.innerHTML =
      `<span class="accent-swatch" style="background:${a.hex};color:${accent.inkOn(a.hex)}">` +
      `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
      `<span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => choose(a.key));
    menu.appendChild(li);
  }

  // A page-only custom accent shows as "Custom" + its live swatch, selecting no preset row.
  let customHex = null;
  const sync = () => {
    if (customHex) {
      curSw.style.background = customHex;
      curName.textContent = 'Custom';
      for (const li of menu.children) li.setAttribute('aria-selected', 'false');
      return;
    }
    const m = meta(value);
    curSw.style.background = m.hex;
    curName.textContent = m.label;
    for (const li of menu.children)
      li.setAttribute('aria-selected', li.dataset.key === value ? 'true' : 'false');
  };
  const onDocPtr = (e) => { if (!mount.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') close(); };
  // The list pours out of the CARET (dropdownMenu.js menuDustPoint), like every enhanced select here.
  const open = () => { menu.hidden = false; surfaceIn(menu, menuDustPoint(trigger)); trigger.setAttribute('aria-expanded', 'true'); document.addEventListener('pointerdown', onDocPtr, true); document.addEventListener('keydown', onKey); };
  const close = () => { if (!menu.hidden) surfaceOut(menu, menuDustPoint(trigger)); menu.hidden = true; trigger.setAttribute('aria-expanded', 'false'); document.removeEventListener('pointerdown', onDocPtr, true); document.removeEventListener('keydown', onKey); };
  // The wipe starts at the trigger's colour SWATCH: the menu is gone by the time the palette
  // floods, and the new colour should visibly pour out of the rect that shows it.
  const choose = (key) => { customHex = null; value = accent.set(key, trigger.querySelector('.js-cur-sw') || trigger); sync(); close(); };

  trigger.addEventListener('click', () => { menu.hidden ? open() : close(); });
  sync();

  // The logo's own gestures move the accent too: a preset shows that preset, a custom hex "Custom".
  window.addEventListener('stencil:accent-changed', (e) => {
    const v = typeof e.detail === 'string' ? e.detail : null;
    if (v && v.charAt(0) === '#') { customHex = v; }
    else { customHex = null; value = accent.get(); }
    sync();
  });
}

// Click cycles, double-click picks a custom colour, right-click / Alt opens a preset menu.
wireLogoAccent(document.querySelector('.brand .logo'));

const themePref = window.StencilTheme;
const appearance = document.getElementById('appearance');
if (themePref && appearance) {
  const syncAppearance = () => { appearance.value = themePref.get(); };
  // Choosing from a native dropdown fires no pointerdown, so the wipe anchors to the <select> itself.
  appearance.addEventListener('change', () => { themePref.set(appearance.value, appearance); syncAppearance(); });
  themePref.onChange(syncAppearance);
  syncAppearance();
}

const motionPref = window.StencilMotion;
const motionSel = document.getElementById('motion');
if (motionPref && motionSel) {
  for (const [key, label] of motionPref.labels) {
    const opt = document.createElement('option');
    opt.value = key;
    opt.textContent = label;
    motionSel.appendChild(opt);
  }
  const syncMotion = () => { motionSel.value = motionPref.get(); };
  motionSel.addEventListener('change', () => { motionPref.set(motionSel.value); syncMotion(); });
  motionPref.onChange(syncMotion);
  syncMotion();
}
