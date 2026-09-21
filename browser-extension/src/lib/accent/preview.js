// The logo accent menu's hover preview, hardened against the palette flood's own churn
// (browser twin: picker.js): a preview fires only after a rest AND only when the key
// CHANGES; the restore waits the flood out before checking the pointer has really left.

// How long the pointer must rest on a row before it previews.
const PREVIEW_HOVER_MS = 280;

// The accent flood (a View Transition or its class fallback) drops real :hover and fires
// synthetic enter/leave, so `:hover` lies while one plays.
const swapping = () => {
  const c = document.documentElement && document.documentElement.classList;
  return !!c && (c.contains('theme-instant') || c.contains('theme-swapping'));
};
// A flood never fires a pointermove, so the last one is the pointer's real position.
let lastPt = null;
let tracking = false;
const trackPointer = () => {
  if (tracking) return;
  tracking = true;
  document.addEventListener('pointermove', (e) => { lastPt = { x: e.clientX, y: e.clientY }; }, true);
  // A null relatedTarget is the window boundary: no pointermove follows, so the last
  // position would stand as a stale in-menu one and the preview would never revert.
  document.addEventListener('pointerout', (e) => { if (!e.relatedTarget) lastPt = null; }, true);
};
const pointerIn = (el) => {
  if (!lastPt) return false;
  const r = el.getBoundingClientRect();
  return lastPt.x >= r.left && lastPt.x <= r.right && lastPt.y >= r.top && lastPt.y <= r.bottom;
};

export const createAccentPreview = ({ menu, logo, accent: A, closeMenu }) => {
  trackPointer();
  let shownKey = null;        // the key currently previewed, or null = the committed accent
  let committing = false;     // a pick's own flood is playing under the closing list
  let hoverTimer = null;
  let leaveTimer = null;
  const clearHover = () => { if (hoverTimer) { clearTimeout(hoverTimer); hoverTimer = null; } };
  const clearLeave = () => { if (leaveTimer) { clearTimeout(leaveTimer); leaveTimer = null; } };
  // A hovered row looks SELECTED via aria-selected, an attribute, so it survives the
  // flood's :hover drop. markSel() puts the committed accent's mark back.
  const markKey = (key) => {
    for (const li of menu.children)
      li.setAttribute('aria-selected', li.dataset.key === key ? 'true' : 'false');
  };
  const markSel = () => {
    const inline = document.documentElement.style.getPropertyValue('--accent');
    markKey(inline ? null : A.get());
  };
  const setHovered = (li) => { if (li) { markKey(li.dataset.key); latchHover(li); } };
  // One beat first, so the flood about to start is up before the poll looks for it.
  const afterSwap = (fn) => {
    const poll = () => { if (swapping()) { setTimeout(poll, 60); return; } fn(); };
    setTimeout(poll, 60);
  };
  // `dd-preview-hold` freezes the rows' hover replays while a preview shows: each flood
  // drops and restores :hover, which would restart them (lib/animations/iconHover.css).
  const holdReplays = (on) => {
    menu.classList[on ? 'add' : 'remove']('dd-preview-hold');
    holdCursor(on);
  };
  // The flood's snapshot steals the row's hit test, so the hand blinks to an arrow; held
  // only while that transition is up and self-releasing.
  const holdCursor = (on) => {
    const root = document.documentElement;
    if (root && root.classList) root.classList[on ? 'add' : 'remove']('dd-preview-cursor');
    if (on) afterSwap(() => { if (root && root.classList) root.classList.remove('dd-preview-cursor'); });
  };
  const latchHover = (li) => {
    for (const r of menu.children) r.classList.toggle('dd-hover', r === li);
  };
  // The preview ends FIRST: marking before the inline override is back would read a
  // custom accent as its stored preset.
  const restore = () => {
    clearHover();
    if (shownKey !== null) { shownKey = null; A.endAccentPreview(logo); }
    markSel();
    // A pick's close lands here mid-flood: the commit's afterSwap releases hold and latch.
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
    // The COMMIT floods too: hold the rows' replays until it settles.
    committing = true;
    holdCursor(false);
    afterSwap(() => { committing = false; holdReplays(false); latchHover(null); });
    shownKey = key; A.set(key, logo); markSel(); closeMenu();
  };
  // A view transition's snapshot tree owns the hit test while it plays, so a row's own click
  // never fires; nothing MOVES in a swap, so hit-test against the rows' boxes.
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
    if (menu.hidden || menu.contains(e.target)) return;
    const li = rowAt(e.clientX, e.clientY);
    if (!li) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    pick(li.dataset.key);
  };
  if (typeof window !== 'undefined' && window.addEventListener)
    window.addEventListener('pointerdown', onStolenPress, true);

  const onRowEnter = (li, key) => {
    clearLeave();
    // Synthetic enters: the flood's, and those a list closing over its own commit drags
    // past the pointer — a hidden list sends no pointerleave, so nothing would revert.
    if (swapping() || committing) return;
    if (!li.classList.contains('dd-hover')) holdReplays(false);
    setHovered(li);
    if (shownKey === key) return;
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
