import { ACCENTS, DEFAULT_ACCENT, accentHex, normalizeHex } from '../core/accents.js';
import { wireLogoAccentMenu } from './logoAccentMenu.js';

// Double-click (or double-tap) the logo opens a native colour picker that tints THIS
// page's accent only — not saved, not synced, gone on reload; a Visuals preset clears
// it (DrawingApp#applyAccent). Exported for tests/logoAccentMenu.test.js.
export function wireLogoColorPicker(logo, app) {
  if (!logo || !app) return;
  logo.style.cursor = 'pointer';
  const wrap = logo.closest?.('.app-logo-wrap') || logo;

  // ── Hover latch (.logo-hover) ── the pulse/ray loop (animations/iconHover.css) keys on this
  // class, NOT :hover: the browser force-drops page hover for the whole accent/theme
  // view transition. themeSwap raises `theme-instant` on <html> for exactly that
  // window — hold the latch through it, then trust real :hover once the swap ends.
  const setHover = (on) => wrap.classList?.toggle('logo-hover', on);
  wrap.addEventListener('pointerenter', () => setHover(true));
  wrap.addEventListener('pointerleave', () => {
    const root = document.documentElement;
    // The latch is ANIMATION state only — the menu's peek lifetime is the gesture
    // machine's business (glide/altRelease/linger below, same as every toolbar icon).
    if (!root.classList?.contains('theme-instant')) { setHover(false); return; }
    const settle = () => {
      if (root.classList.contains('theme-instant')) { setTimeout(settle, 60); return; }
      // one beat for the browser to re-establish real hover, then trust it
      setTimeout(() => { if (!wrap.matches(':hover')) setHover(false); }, 90);
    };
    settle();
  });

  const { menuShowing, closeMenu, altPeek, menuKind } = wireLogoAccentMenu(logo, wrap, app);

  // Single-click cycles the accent to the next preset (a CUSTOM colour resets to the
  // default), deferred briefly so a double-click cancels it. The logo is handed over as
  // the swap origin — the palette floods out of the badge you clicked (desktop parity:
  // mainWindow.cpp anchors its accent cycle to the logo too).
  const cycleAccent = () => {
    if (app.customAccent) { app.setAccent(DEFAULT_ACCENT, logo); return; }
    const keys = ACCENTS.map((a) => a.key);
    const i = keys.indexOf(app.accent);
    app.setAccent(keys[(i + 1) % keys.length], logo);
  };
  let clickTimer = null;
  logo.addEventListener('click', (e) => {
    // Alt+click opens the menu as a PEEK instead of cycling (never schedules the
    // deferred cycle). Menu already open: an Alt-opened one treats the click as part
    // of the hold gesture (no-op — Alt's release governs); a sticky one toggles closed.
    if (e.altKey) {
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
      if (menuShowing()) { if (menuKind() !== 'peek') closeMenu(); return; }
      altPeek();
      return;
    }
    if (clickTimer) return;   // second click of a dbl — let dblclick handle it
    clickTimer = setTimeout(() => { clickTimer = null; cycleAccent(); }, 220);
  });

  // A tiny, near-invisible colour input parked under the logo. It stays in normal flow
  // (not display:none / zero-size) so the browser will actually render its native picker.
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.setAttribute('aria-hidden', 'true');
  picker.tabIndex = -1;
  picker.style.cssText = 'position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;';
  logo.insertAdjacentElement('afterend', picker);

  // Same origin as the cycle: the native picker is an OS window, so there is no press in
  // the page to read while the user drags around it.
  const apply = () => app.setCustomAccent(picker.value, logo); // native colour input yields #rrggbb
  picker.addEventListener('input', apply);   // live while dragging
  picker.addEventListener('change', apply);  // final commit

  const open = () => {
    const cur = app.customAccent || getComputedStyle(document.documentElement).getPropertyValue('--accent');
    picker.value = normalizeHex(cur) || accentHex(app.accent);
    // showPicker() is the reliable way to open a picker programmatically (a bare .click()
    // on a hidden input often won't); fall back to click() on older browsers.
    try {
      if (typeof picker.showPicker === 'function') picker.showPicker();
      else picker.click();
    } catch {
      picker.click();
    }
  };
  logo.addEventListener('dblclick', () => {
    if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }   // cancel the single-click cycle
    open();
  });
  // A double-click selects nearby text; clear it so the picker isn't fighting a selection.
  logo.addEventListener('mousedown', (e) => { if (e.detail > 1) e.preventDefault(); });

  // dblclick is unreliable on touch — detect a double-tap by hand.
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
