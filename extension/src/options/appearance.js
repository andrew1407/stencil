// ── Appearance: accent, theme and interface animation ───────────────────────
// All three live in localStorage via lib/accent.js, not in the saved settings, so they
// apply instantly and flash-free across the extension's pages — no Save button.
import { surfaceIn, surfaceOut } from '../lib/motion.js';
import { icon } from '../lib/icons.js';
import { menuDustPoint } from '../lib/dropdownMenu.js';
import { wireLogoAccent } from '../lib/logoAccent.js';

// Theme accent — persisted separately in localStorage (window.StencilAccent, set
// up by lib/accent.js) so it applies flash-free across the extension's pages. It
// is independent of the Save button: changing it applies + persists instantly.
const accent = window.StencilAccent;
if (accent) {
  // Custom colour-swatch dropdown — a colour RECT + name per option, since a
  // native <select> can't paint per-option swatches on every OS (e.g. macOS).
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
    // Every chip carries the ✓; only the selected row reveals it (lib/theme/select.css) — the
    // browser's accentPicker.js row, and the desktop's painted tick.
    li.innerHTML =
      `<span class="accent-swatch" style="background:${a.hex};color:${accent.inkOn(a.hex)}">` +
      `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
      `<span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => choose(a.key));
    menu.appendChild(li);
  }

  // A page-only custom accent (the logo's double-click) shows as "Custom" + its live
  // swatch, selecting no preset row — exactly like the browser's accent picker trigger.
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
  // The swatch list pours out of the CARET the pointer pressed and back into it, the
  // same corner every enhanced select on this page grows from (dropdownMenu.js
  // menuDustPoint) — off the trigger's centre it formed in the middle of the label.
  const open = () => { menu.hidden = false; surfaceIn(menu, menuDustPoint(trigger)); trigger.setAttribute('aria-expanded', 'true'); document.addEventListener('pointerdown', onDocPtr, true); document.addEventListener('keydown', onKey); };
  const close = () => { if (!menu.hidden) surfaceOut(menu, menuDustPoint(trigger)); menu.hidden = true; trigger.setAttribute('aria-expanded', 'false'); document.removeEventListener('pointerdown', onDocPtr, true); document.removeEventListener('keydown', onKey); };
  // The wipe starts at the trigger's colour SWATCH, not at the option row: the menu is
  // gone by the time the palette floods, and a row near the top of a scrolled menu
  // would look like the colour came out of a corner. The swatch, not the whole trigger:
  // the new colour should visibly pour out of the little rect that shows it, not out
  // of the middle of the text.
  const choose = (key) => { customHex = null; value = accent.set(key, trigger.querySelector('.js-cur-sw') || trigger); sync(); close(); };

  trigger.addEventListener('click', () => { menu.hidden ? open() : close(); });
  sync();

  // Keep the trigger in step when the logo's own gestures move the accent: a preset
  // (cycle / menu) shows that preset; a custom hex (double-click picker) shows "Custom".
  // The event carries the new value in `detail`.
  window.addEventListener('stencil:accent-changed', (e) => {
    const v = typeof e.detail === 'string' ? e.detail : null;
    if (v && v.charAt(0) === '#') { customHex = v; }
    else { customHex = null; value = accent.get(); }
    sync();
  });
}

// The header logo gets the browser toolbar logo's accent gestures: click cycles, double-
// click picks a custom colour, right-click / Alt opens a preset menu with hover preview.
wireLogoAccent(document.querySelector('.brand .logo'));

// Appearance — like the accent, it lives in localStorage via lib/accent.js rather than
// in the saved settings, so it applies instantly, independent of the Save button.
const themePref = window.StencilTheme;
const appearance = document.getElementById('appearance');
if (themePref && appearance) {
  const syncAppearance = () => { appearance.value = themePref.get(); };
  // Anchor the wipe to the <select> itself. Choosing from a native dropdown fires no
  // pointerdown in the page, so there is no press to read — and this page has no
  // #theme-toggle to fall back to either.
  appearance.addEventListener('change', () => { themePref.set(appearance.value, appearance); syncAppearance(); });
  themePref.onChange(syncAppearance);
  syncAppearance();
}

// Interface animation — the same instant, localStorage-backed recipe (lib/accent.js
// StencilMotion). The options come from the script's own label list.
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
