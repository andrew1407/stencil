import { surfaceIn, surfaceOut, centerOf } from './motion.js';
import { icon } from './icons.js';

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
  let committing = false;     // a pick's own flood is playing under the closing list
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
  const setHovered = (li) => { if (li) { markKey(li.dataset.key); latchHover(li); } };
  // Run once the flood a call is about to start has finished — one beat first, so that
  // transition is up before the poll looks for it.
  const afterSwap = (fn) => {
    const poll = () => { if (swapping()) { setTimeout(poll, 60); return; } fn(); };
    setTimeout(poll, 60);
  };
  // `dd-preview-hold` freezes the rows' hover replays for as long as a preview shows: each
  // flood drops and restores :hover, which restarted them (lib/animations.css).
  const holdReplays = (on) => {
    menu.classList[on ? 'add' : 'remove']('dd-preview-hold');
    holdCursor(on);
  };
  // The flood's snapshot steals the row's hit test, so the hand blinks to an arrow. Held
  // only while that transition is up, and self-releasing, so a menu dismissed mid-preview
  // strands no hand on the page.
  const holdCursor = (on) => {
    const root = document.documentElement;
    if (root && root.classList) root.classList[on ? 'add' : 'remove']('dd-preview-cursor');
    if (on) afterSwap(() => { if (root && root.classList) root.classList.remove('dd-preview-cursor'); });
  };
  // The hovered row's own :hover treatment (the 2px slide), LATCHED as a class — the flood
  // drops real :hover, and the row snapped back and slid in again after every preview.
  const latchHover = (li) => {
    for (const r of menu.children) r.classList.toggle('dd-hover', r === li);
  };
  // The preview ends FIRST: it strips the inline override, so marking before it came back
  // would read a custom accent as its stored preset.
  const restore = () => {
    clearHover();
    if (shownKey !== null) { shownKey = null; A.endAccentPreview(logo); }
    markSel();
    // A pick closes the list ON TOP of its own flood, and that close lands here: leave the
    // hold and the latch to the commit's afterSwap below, or the dissolving rows replay.
    if (committing) return;
    latchHover(null);
    holdReplays(false);
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
  const pick = (key) => {
    clearHover(); clearLeave();
    // The COMMIT floods too, and the list is closing over it: hold the rows' replays
    // until that one has settled as well, and let the cursor go with the menu.
    committing = true;
    holdCursor(false);
    afterSwap(() => { committing = false; holdReplays(false); latchHover(null); });
    shownKey = key; A.set(key, logo); markSel(); closeMenu();
  };
  // The press a FLOOD swallowed — browser accentPicker.js twin, same reason. A view
  // transition's snapshot tree is an overlay over the whole page and it owns the hit test
  // while it plays: pressing a row mid-wipe answers <html>, so the row's own click never
  // fires AND the outside-press check reads the press as a dismissal — picking a colour
  // while its own hover preview was still wiping closed the list and put the old accent
  // back (user report). `pointer-events` on ::view-transition does not help; the
  // transition ROOT is what takes the hit. Nothing moves in these swaps — only the
  // palette changes — so the rows are exactly where the snapshot draws them and the press
  // can be resolved against their boxes. On the WINDOW, in capture, so it runs before any
  // document-level dismissal whichever was registered first.
  const rowAt = (x, y) => {
    if (menu.hidden) return null;
    for (const li of menu.children) {
      if (!li.classList || !li.classList.contains('accent-dd-opt')) continue;
      const r = li.getBoundingClientRect();
      if (r.width && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return li;
    }
    return null;
  };
  const onStolenPress = (e) => {
    // A press the rows received themselves needs nothing; this is only the stolen case.
    if (menu.hidden || menu.contains(e.target)) return;
    const li = rowAt(e.clientX, e.clientY);
    if (!li) return;
    e.preventDefault();
    e.stopImmediatePropagation();   // …and no dismissal may see it
    pick(li.dataset.key);
  };
  if (typeof window !== 'undefined' && window.addEventListener)
    window.addEventListener('pointerdown', onStolenPress, true);

  const fill = () => {
    if (menu.childElementCount) return;
    for (const a of A.list) {
      const li = document.createElement('li');
      li.className = 'accent-dd-opt';
      li.setAttribute('role', 'option');
      li.dataset.key = a.key;
      li.innerHTML =
        `<span class="accent-swatch" style="background:${a.hex}">` +
        `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
        `<span class="accent-dd-name">${a.label}</span>`;
      li.addEventListener('click', () => pick(a.key));
      li.addEventListener('pointerenter', () => {
        clearLeave();
        // Synthetic enters — the flood's, and the ones a list closing over its own commit
        // drags past the pointer. The latter would start a preview nothing ever reverts
        // (a hidden list sends no pointerleave), stranding a colour nobody chose.
        if (swapping() || committing) return;
        // A row the pointer has NOT been resting on is a real hop: lift the hold so it
        // plays its own hover once.
        if (!li.classList.contains('dd-hover')) holdReplays(false);
        setHovered(li);           // the hovered row takes the selected style
        if (shownKey === a.key) return;   // already showing this one
        clearHover();
        hoverTimer = setTimeout(() => {
          hoverTimer = null;
          if (shownKey === a.key) return;
          shownKey = a.key;
          holdReplays(true);
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
