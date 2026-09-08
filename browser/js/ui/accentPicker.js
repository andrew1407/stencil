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
  let committing = false;     // a pick's own flood is playing under the closing list
  const currentSel = () => {
    const r = menu.querySelector('.accent-dd-opt[aria-selected="true"]');
    return r ? r.dataset.key : null;
  };
  const setHovered = (li) => {
    if (!li) return;
    if (committedSel === null) committedSel = currentSel();
    markSelected(menu, li.dataset.key);
    latchHover(li);
  };
  // Run once the flood a call is about to start has finished — one beat first, so that
  // transition is up before the poll looks for it.
  const afterSwap = (fn) => {
    const poll = () => { if (swapping()) { setTimeout(poll, 60); return; } fn(); };
    setTimeout(poll, 60);
  };
  // `dd-preview-hold` freezes the rows' hover replays for as long as a preview shows —
  // each flood drops and restores :hover, which restarted them (animations.css).
  const holdReplays = (on) => {
    menu.classList?.[on ? 'add' : 'remove']('dd-preview-hold');
    holdCursor(on);
  };
  // The flood's snapshot steals the row's hit test, so the hand blinks to an arrow. Held
  // only while that transition is up, and self-releasing, so a list dismissed mid-preview
  // strands no hand on the page.
  const holdCursor = (on) => {
    const root = document.documentElement;
    root?.classList?.[on ? 'add' : 'remove']('dd-preview-cursor');
    if (on) afterSwap(() => root?.classList?.remove('dd-preview-cursor'));
  };
  // The hovered row's own :hover treatment (the 2px slide), LATCHED as a class — the flood
  // drops real :hover, and the row snapped back and slid in again after every preview.
  const latchHover = (li) => {
    for (const r of menu.children) r.classList?.toggle('dd-hover', r === li);
  };
  const restore = () => {
    clearHover();
    if (committedSel !== null) { markSelected(menu, committedSel); committedSel = null; }
    if (preview && shownKey !== null) { shownKey = null; preview.off(); }
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
    committedSel = null; shownKey = key; onPick(key);
  };
  // The press a FLOOD swallowed. A view transition's snapshot tree is an overlay over the
  // whole page and it owns the hit test while it plays: pressing a row mid-wipe answers
  // <html>, so the row's own click never fires AND the list's outside-press check reads
  // the press as a dismissal — picking a colour while its own hover preview was still
  // wiping closed the list and put the old accent back (user report). `pointer-events` on
  // ::view-transition does not help; the transition ROOT is what takes the hit.
  // Nothing moves in these swaps — only the palette changes — so the rows are exactly
  // where the snapshot draws them and the press can be resolved against their boxes.
  // On the WINDOW, in capture: it must run before any document-level dismissal, whichever
  // was registered first (the logo menu builds its rows lazily, on the first open).
  const rowAt = (x, y) => {
    if (menu.hidden) return null;
    for (const li of menu.children) {
      if (!li.classList?.contains('accent-dd-opt') || li.style.display === 'none') continue;
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

  for (const a of ACCENTS) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    li.innerHTML =
      `<span class="accent-swatch" style="background:${a.hex}">` +
      `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
      `<span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => pick(a.key));
    if (preview) {
      li.addEventListener('pointerenter', () => {
        clearLeave();
        // Synthetic enters — the flood's, and the ones a list closing over its own commit
        // drags past the pointer. The latter would start a preview nothing ever reverts
        // (a hidden list sends no pointerleave), stranding a colour nobody chose.
        if (swapping() || committing) return;
        // A row the pointer has NOT been resting on is a real hop: lift the hold so it
        // plays its own hover once.
        if (!li.classList?.contains?.('dd-hover')) holdReplays(false);
        setHovered(li);           // the hovered row takes the selected style + ✓
        if (shownKey === a.key) return;   // already showing this one
        clearHover();
        hoverTimer = setTimeout(() => {
          hoverTimer = null;
          if (shownKey === a.key) return;
          shownKey = a.key;
          holdReplays(true);
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
  // A list can also be DISMISSED under the pointer (Escape, an outside press, a pick):
  // its owner calls this so no latch or hold outlives it and greets the next open.
  return restore;
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

  const resetHover = fillAccentMenu(menu, (key) => choose(key), preview);

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
    resetHover();     // a menu dismissed mid-hover reverts to the committed accent, and
    preview?.off();   // drops the row's latched hover with it
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
