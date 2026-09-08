import { surfaceIn, surfaceOut, centerOf } from './motion.js';

// How long the pointer must rest on a row before it previews. Browser twin: accentPicker.js.
const PREVIEW_HOVER_MS = 280;

// The accent flood is a View Transition ('theme-instant') or its class fallback
// ('theme-swapping'); either way real :hover is dropped and synthetic enter/leave fire,
// so `:hover` lies while one plays. These read the truth instead.
const swapping = () => {
  const c = document.documentElement && document.documentElement.classList;
  return !!c && (c.contains('theme-instant') || c.contains('theme-swapping'));
};
// The pointer's real position — a flood never fires a pointermove. One listener for every
// menu (a `:hover` check looped here: restore fired after each flood, re-arming the next).
let lastPt = null;
let tracking = false;
const trackPointer = () => {
  if (tracking) return;
  tracking = true;
  document.addEventListener('pointermove', (e) => { lastPt = { x: e.clientX, y: e.clientY }; }, true);
};
const pointerIn = (el) => {
  if (!lastPt) return false;
  const r = el.getBoundingClientRect();
  return lastPt.x >= r.left && lastPt.x <= r.right && lastPt.y >= r.top && lastPt.y <= r.bottom;
};

// The extension's header logo, wired with the browser toolbar logo's accent gestures
// (browser/js/ui/toolbar.js wireLogoColorPicker), against window.StencilAccent:
//   • CLICK        — cycle the accent to the next preset (deferred so a double-click cancels it)
//   • DOUBLE-CLICK — a native colour picker for a PAGE-ONLY custom accent (gone on reload)
//   • RIGHT-CLICK / Alt(+click) / Alt+hover — a preset menu with hover preview
// The menu hangs off the logo badge and reuses the page's own .accent-dd-menu styling; a
// row previews on hover (StencilAccent.previewAccent) and reverts on leave/close, so you
// can see a colour before committing. A pick commits through StencilAccent.set.
export function wireLogoAccent(logo) {
  const A = typeof window !== 'undefined' && window.StencilAccent;
  if (!logo || !A) return;
  const wrap = logo.closest?.('.logo-wrap') || logo;
  logo.style.cursor = 'pointer';

  // ── Preset menu ──
  let menu = wrap.querySelector?.('.logo-accent-menu');
  if (!menu) {
    menu = document.createElement('ul');
    menu.className = 'accent-dd-menu logo-accent-menu';
    menu.setAttribute('role', 'listbox');
    menu.setAttribute('aria-label', 'Color theme');
    menu.hidden = true;
    wrap.appendChild(menu);
  }
  // Hover preview, hardened against the flood's own churn (browser twin: accentPicker.js):
  // it fires only after a rest AND only when the key CHANGES, and the restore waits out the
  // flood before checking that the pointer has really left. Without both, the synthetic
  // enter/leave ping-ponged preview → restore → preview forever (user report).
  trackPointer();
  let shownKey = null;        // the key currently previewed, or null = the committed accent
  let hoverTimer = null;
  let leaveTimer = null;
  const clearHover = () => { if (hoverTimer) { clearTimeout(hoverTimer); hoverTimer = null; } };
  const clearLeave = () => { if (leaveTimer) { clearTimeout(leaveTimer); leaveTimer = null; } };
  // A hovered row looks SELECTED (user decision) — via aria-selected, an attribute, so it
  // survives the flood's :hover drop. markSel() puts the committed accent's mark back.
  const markKey = (key) => {
    for (const li of menu.children)
      li.setAttribute('aria-selected', li.dataset.key === key ? 'true' : 'false');
  };
  // A live custom hex (the double-click picker) is no preset, so it marks NO row —
  // browser twin: toolbar.js passes null for app.customAccent.
  const markSel = () => {
    const inline = document.documentElement.style.getPropertyValue('--accent');
    markKey(inline ? null : A.get());
  };
  const setHovered = (li) => { if (li) markKey(li.dataset.key); };
  // The preview ends FIRST: it strips the inline override, so marking before it came back
  // would read a custom accent as its stored preset.
  const restore = () => {
    clearHover();
    if (shownKey !== null) { shownKey = null; A.endAccentPreview(logo); }
    markSel();
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
  const fill = () => {
    if (menu.childElementCount) return;
    for (const a of A.list) {
      const li = document.createElement('li');
      li.className = 'accent-dd-opt';
      li.setAttribute('role', 'option');
      li.dataset.key = a.key;
      li.innerHTML =
        `<span class="accent-swatch" style="background:${a.hex}"></span>` +
        `<span class="accent-dd-name">${a.label}</span>`;
      li.addEventListener('click', () => { clearHover(); clearLeave(); shownKey = a.key; A.set(a.key, logo); markSel(); closeMenu(); });
      li.addEventListener('pointerenter', () => {
        clearLeave();
        if (swapping()) return;   // synthetic enter from the flood — keep the current highlight
        setHovered(li);           // the hovered row takes the selected style
        if (shownKey === a.key) return;   // already showing this one
        clearHover();
        hoverTimer = setTimeout(() => {
          hoverTimer = null;
          if (shownKey === a.key) return;
          shownKey = a.key;
          A.previewAccent(a.key, logo);
        }, PREVIEW_HOVER_MS);
      });
      li.addEventListener('pointerleave', clearHover);
      menu.appendChild(li);
    }
    // Leaving the list reverts — but only once the flood has settled and the pointer has
    // really gone (a synthetic mid-flood leave, or a hop between rows, must not).
    menu.addEventListener('pointerleave', scheduleRestore);
  };
  const menuShowing = () => !menu.hidden;
  const onDocDown = (e) => { if (!wrap.contains(e.target)) closeMenu(); };
  const onKey = (e) => { if (e.key === 'Escape') closeMenu(); };
  const openMenu = () => {
    fill();
    markSel();
    // Show the whole list when the window has room; only cap (and scroll) when it does
    // not — the space under the logo, overriding the shared 280px .accent-dd-menu cap.
    // Browser twin: toolbar.js openMenu sizes the logo menu the same way.
    const r = wrap.getBoundingClientRect?.();
    if (r && typeof window !== 'undefined' && typeof window.innerHeight === 'number')
      menu.style.maxHeight = `${Math.max(120, window.innerHeight - r.bottom - 16)}px`;
    menu.hidden = false;
    wrap.classList.add('logo-menu-open');   // lift the badge's stacking context over the page
    surfaceIn(menu, centerOf(wrap));   // pours out of the logo, like every extension surface
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  const closeMenu = () => {
    if (menu.hidden) return;
    restore();
    surfaceOut(menu, centerOf(wrap));
    menu.hidden = true;
    wrap.classList.remove('logo-menu-open');
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey);
  };
  wrap.addEventListener('contextmenu', (e) => { e.preventDefault(); menuShowing() ? closeMenu() : openMenu(); });
  wrap.addEventListener('mouseenter', (e) => { if (e.altKey && !menuShowing()) openMenu(); });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Alt' && wrap.matches?.(':hover') && !menuShowing()) { e.preventDefault?.(); openMenu(); }
  });

  // ── Click cycles; double-click opens the custom picker ──
  const cycle = () => {
    const keys = A.list.map((a) => a.key);
    const i = keys.indexOf(A.get());
    A.set(keys[(i + 1) % keys.length], logo);
    markSel();
  };
  let clickTimer = null;
  logo.addEventListener('click', (e) => {
    if (e.altKey) {   // Alt+click toggles the menu instead of cycling
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
      menuShowing() ? closeMenu() : openMenu();
      return;
    }
    if (clickTimer) return;   // the second click of a double — dblclick handles it
    clickTimer = setTimeout(() => { clickTimer = null; cycle(); }, 220);
  });

  // A near-invisible colour input parked by the logo — the native picker needs a real,
  // rendered element (not display:none) to open from.
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.setAttribute('aria-hidden', 'true');
  picker.tabIndex = -1;
  picker.style.cssText = 'position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;';
  logo.insertAdjacentElement('afterend', picker);
  const applyCustom = () => A.setCustom(picker.value, logo);
  picker.addEventListener('input', applyCustom);    // live while dragging
  picker.addEventListener('change', applyCustom);   // final commit
  logo.addEventListener('dblclick', () => {
    if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }   // cancel the pending cycle
    const cur = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
    picker.value = /^#[0-9a-fA-F]{6}$/.test(cur) ? cur : A.hexOf(A.get());
    try { if (typeof picker.showPicker === 'function') picker.showPicker(); else picker.click(); }
    catch { picker.click(); }
  });
  // A double-click selects nearby text; clear it so the picker isn't fighting a selection.
  logo.addEventListener('mousedown', (e) => { if (e.detail > 1) e.preventDefault(); });
}
