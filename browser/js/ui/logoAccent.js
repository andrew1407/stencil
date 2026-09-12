import { ACCENTS, DEFAULT_ACCENT, accentHex, normalizeHex } from '../core/accents.js';
import { wireLogoAccentMenu } from './logoAccentMenu.js';

// Double-click (or double-tap) the logo opens a native colour picker that tints this page's
// accent only — not saved, not synced; a Visuals preset clears it. Exported for tests.
export function wireLogoColorPicker(logo, app) {
  if (!logo || !app) return;
  logo.style.cursor = 'pointer';
  const wrap = logo.closest?.('.app-logo-wrap') || logo;

// Hover latch: the pulse/ray loop (animations/iconHover.css) keys on .logo-hover, not
// :hover — the browser drops page hover for the whole accent/theme view transition
// (themeSwap raises `theme-instant` on <html> for that window).
  const setHover = (on) => wrap.classList?.toggle('logo-hover', on);
  wrap.addEventListener('pointerenter', () => setHover(true));
  wrap.addEventListener('pointerleave', () => {
    const root = document.documentElement;
    // Animation state only — the menu's peek lifetime is the gesture machine's business.
    if (!root.classList?.contains('theme-instant')) { setHover(false); return; }
    const settle = () => {
      if (root.classList.contains('theme-instant')) { setTimeout(settle, 60); return; }
      setTimeout(() => { if (!wrap.matches(':hover')) setHover(false); }, 90);
    };
    settle();
  });

  const { menuShowing, closeMenu, altPeek, menuKind } = wireLogoAccentMenu(logo, wrap, app);

// Single-click cycles the preset (a custom colour resets to the default), deferred so a
// double-click cancels it; the logo is the swap origin (desktop: mainWindow.cpp too).
  const cycleAccent = () => {
    if (app.customAccent) { app.setAccent(DEFAULT_ACCENT, logo); return; }
    const keys = ACCENTS.map((a) => a.key);
    const i = keys.indexOf(app.accent);
    app.setAccent(keys[(i + 1) % keys.length], logo);
  };
  let clickTimer = null;
  logo.addEventListener('click', (e) => {
// Alt+click opens the menu as a peek instead of cycling; an Alt-opened menu treats the
// click as part of the hold, a sticky one toggles closed.
    if (e.altKey) {
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
      if (menuShowing()) { if (menuKind() !== 'peek') closeMenu(); return; }
      altPeek();
      return;
    }
    if (clickTimer) return;   // second click of a dbl — let dblclick handle it
    clickTimer = setTimeout(() => { clickTimer = null; cycleAccent(); }, 220);
  });

// Kept in normal flow (not display:none / zero-size) or the browser won't open its picker.
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.setAttribute('aria-hidden', 'true');
  picker.tabIndex = -1;
  picker.style.cssText = 'position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;';
  logo.insertAdjacentElement('afterend', picker);

// Same origin as the cycle: the native picker is an OS window, so there is no press to read.
  const apply = () => app.setCustomAccent(picker.value, logo);
  picker.addEventListener('input', apply);   // live while dragging
  picker.addEventListener('change', apply);  // final commit

  const open = () => {
    const cur = app.customAccent || getComputedStyle(document.documentElement).getPropertyValue('--accent');
    picker.value = normalizeHex(cur) || accentHex(app.accent);
    // showPicker() is the reliable way; a bare .click() on a hidden input often won't.
    try {
      if (typeof picker.showPicker === 'function') picker.showPicker();
      else picker.click();
    } catch {
      picker.click();
    }
  };
  logo.addEventListener('dblclick', () => {
    if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
    open();
  });
  logo.addEventListener('mousedown', (e) => { if (e.detail > 1) e.preventDefault(); });

  let lastTap = 0;
  logo.addEventListener('touchend', (e) => {
    const now = Date.now();
    if (now - lastTap < 400) {
      e.preventDefault();
      lastTap = 0;
      open();
    } else {
      lastTap = now;
    }
  });
}
