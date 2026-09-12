// ── The logo accent menu's hover preview ────────────────────────────────────
// One state machine, hardened against the palette flood's own churn (browser twin:
// accentPicker.js): a preview fires only after a rest AND only when the key CHANGES, and
// the restore waits the flood out before checking that the pointer has really left.

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

// `closeMenu` is read lazily — the menu that owns this preview defines it after wiring.
export const createAccentPreview = ({ menu, logo, accent: A, closeMenu }) => {
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
  // flood drops and restores :hover, which restarted them (lib/animations/iconHover.css).
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
  // transition's snapshot tree owns the hit test while it plays: pressing a row mid-wipe
  // answers <html>, so the row's click never fires AND the outside-press check reads it as
  // a dismissal. `pointer-events` on ::view-transition does not help; the transition ROOT
  // takes the hit. Nothing MOVES in these swaps, so the press resolves against the rows'
  // own boxes. On the WINDOW, in capture, so it beats any document-level dismissal.
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

  // A row the pointer entered: latch its hover, then preview once the pointer has rested.
  const onRowEnter = (li, key) => {
    clearLeave();
    // Synthetic enters — the flood's, and the ones a list closing over its own commit
    // drags past the pointer. The latter would start a preview nothing ever reverts
    // (a hidden list sends no pointerleave), stranding a colour nobody chose.
    if (swapping() || committing) return;
    // A row the pointer has NOT been resting on is a real hop: lift the hold so it
    // plays its own hover once.
    if (!li.classList.contains('dd-hover')) holdReplays(false);
    setHovered(li);           // the hovered row takes the selected style
    if (shownKey === key) return;   // already showing this one
    clearHover();
    hoverTimer = setTimeout(() => {
      hoverTimer = null;
      if (shownKey === key) return;
      shownKey = key;
      holdReplays(true);
      A.previewAccent(key, logo);
    }, PREVIEW_HOVER_MS);
  };

  return { clearHover, markSel, onRowEnter, pick, restore, scheduleRestore };
};
