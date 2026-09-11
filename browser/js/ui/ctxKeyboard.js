import { isTypingTarget } from '../utils.js';

// ── The context menu's keyboard navigation ──────────────────────
// Split out of contextMenu.js. ↑/↓ walk the rows of the deepest open level, → opens the
// row's flyout (and lands on its first row), ← closes it back onto its parent row,
// Enter/Space picks. The hover path owns the same activeSub state, so it is read and
// written through the host that wire() hands in.
// Keyboard walk (desktop QMenu parity): the row index `step` away from `idx` among
// `count` rows, wrapping at both ends; no row yet ⇒ the first (down) or last (up).
export const ctxKeyStep = (count, idx, step) => {
  if (!count) return -1;
  if (idx < 0) return step > 0 ? 0 : count - 1;
  return (idx + step + count) % count;
};
// The keys the open menu owns outright (contextMenu.js keyboard navigation).
export const CTX_NAV_KEYS = Object.freeze(['ArrowDown', 'ArrowUp', 'ArrowLeft', 'ArrowRight', 'Enter', ' ', 'Home', 'End', 'Tab']);
// What Tab walks inside an open flyout: its real form controls (the Style spinners and
// radios, the tint colour, the formula inputs, the tooltip checkboxes, the chat's input
// and buttons), in markup order, skipping anything currently hidden.
const CTX_FOCUSABLE = 'input, select, textarea, button, [tabindex]:not([tabindex="-1"])';
export const ctxFocusables = (level) => [...level.querySelectorAll(CTX_FOCUSABLE)]
  .filter((el) => !el.disabled && el.getClientRects().length > 0);

export const wireCtxKeyboard = ({ menu, menuIsOpen, chatRowMenuOpen, closeSub, positionSub,
                                 activeSub, setActiveSub }) => {
      // ↑/↓ walk the rows of the deepest open level, → opens the row's flyout (and lands
    // on its first row), ← closes it back onto its parent row, Enter/Space picks. The
    // highlighted row wears .ctx-kb (the :hover look, components.css); a real pointer
    // move hands the highlight back to :hover (samplePointer above).
    let kbItem = null;
    const setKbItem = (item) => {
      if (kbItem && kbItem !== item) kbItem.classList.remove('ctx-kb');
      kbItem = item || null;
      if (kbItem) {
        kbItem.classList.add('ctx-kb');
        kbItem.scrollIntoView?.({ block: 'nearest' });
      }
    };
    // The rows of one level: its DIRECT .ctx-item children that syncState hasn't hidden.
    const kbRows = (level) => [...level.querySelectorAll(':scope > .ctx-item')]
      .filter(i => i.style.display !== 'none');
    // The level the highlight walks: the one holding the highlighted row (rows are direct
    // children of their level), else the deepest open flyout.
    const kbLevel = () => kbItem ? kbItem.parentElement
      : (activeSub()?.classList.contains('ctx-sub-visible')) ? activeSub() : menu;
    const subOf = (item) => item?.querySelector(':scope > .ctx-sub') || null;
    // Where the second → lands: the flyout's text box where it has one (the chat), else
    // its first control (Style's point size, Transformation's checkbox, a filter radio).
    const primaryControl = (sub) => {
      const controls = ctxFocusables(sub);
      return controls.find((el) => el.tagName === 'TEXTAREA') || controls[0] || null;
    };
    // A flyout with no controls (Image / Layout) is entered onto its first row instead.
    const kbEnterSub = (sub) => {
      const control = primaryControl(sub);
      if (control) { control.focus(); setKbItem(null); return; }
      setKbItem(kbRows(sub)[0] || null);
    };
    const kbOpenSub = (item) => {
      const sub = subOf(item);
      if (!sub || item.dataset.noSub === '1') return false;
      // The hover path's own steps (wireSubmenu mouseenter): siblings close, ancestors stay.
      document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(s => {
        if (s !== sub && !s.contains(item)) closeSub(s);
      });
      document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => {
        if (i !== item && !i.contains(item)) i.classList.remove('ctx-open-sub');
      });
      positionSub(item, sub);
      setActiveSub(sub, item);
      setKbItem(item);   // revealed only — ← folds it back, a second → enters it
      return true;
    };
    // Close the deepest open flyout; the highlight lands back on its parent row unless
    // `keepHighlight` (the walk that closed it already moved on).
    const kbCloseSub = (keepHighlight = false) => {
      const sub = activeSub();
      if (!sub) return false;
      const item = sub.__ctxItem;
      if (sub.contains(document.activeElement)) document.activeElement.blur();
      closeSub(sub);
      item?.classList.remove('ctx-open-sub');
      const parentSub = item?.parentElement?.closest('.ctx-sub') || null;
      setActiveSub(parentSub, parentSub ? parentSub.__ctxItem : null);
      if (!keepHighlight) setKbItem(item || null);
      return true;
    };
    // Walking off a parent row folds its flyout (Qt parity: the hover path's plain-item
    // mouseenter does the same) — every open flyout the highlight is no longer inside.
    const closeSubsOutside = (item) => {
      while (activeSub() && !activeSub().contains(item) && activeSub().__ctxItem !== item) {
        if (!kbCloseSub(true)) break;
      }
    };
    // Capture phase, consumed only while open — same as Escape below: the arrows must
    // never reach the canvas pan/nudge binding (controlsBinder.js wireArrowPan). A pan
    // scrolls the viewport, and the scroll closer would then take the menu with it,
    // which is exactly how "the arrows don't work in the menu" looked (user report).
    document.addEventListener('keydown', e => {
      if (!menuIsOpen() || chatRowMenuOpen()) return;
      if (!CTX_NAV_KEYS.includes(e.key)) return;
      const open = activeSub();
    const openSub = (open && open.classList.contains('ctx-sub-visible')) ? open : null;
      // Tab walks the open flyout's own controls (Shift+Tab backwards), wrapping — the
      // browser's default would tab straight out of the menu into the toolbar (user
      // report: the Style / Transformation / Assistant controls were unreachable). A
      // level with no controls (the root, a plain flyout) treats Tab as the arrows.
      if (e.key === 'Tab') {
        const controls = ctxFocusables(openSub || menu);
        if (controls.length) {
          e.preventDefault();
          e.stopPropagation();
          const at = controls.indexOf(document.activeElement);
          controls[ctxKeyStep(controls.length, at, e.shiftKey ? -1 : 1)].focus();
          setKbItem(null);
          return;
        }
      }
      // A control inside the flyout has the keys (typing, spinning, picking a radio);
      // the canvas pan still never sees them. Only ← leaves it, back onto the parent row —
      // except in a text field, where ← moves the caret (Tab / Shift+Tab leave those).
      if (openSub && openSub.contains(document.activeElement) && document.activeElement !== openSub) {
        // A text field keeps every key, Enter included — the chat sends on it, and the
        // canvas pan already ignores typing targets; stopping propagation here would
        // keep the key from ever reaching the field.
        if (isTypingTarget(e.target)) return;
        if (e.key.startsWith('Arrow')) e.stopPropagation();   // never the canvas pan
        if (e.key === 'ArrowLeft') { e.preventDefault(); kbCloseSub(); }
        return;
      }
      if (isTypingTarget(e.target) && menu.contains(e.target)) return;
      e.preventDefault();
      e.stopPropagation();
      const level = kbLevel();
      const rows = kbRows(level);
      const idx = rows.indexOf(kbItem);
      const step = (d) => {
        const n = ctxKeyStep(rows.length, idx, d);
        if (n < 0) return;
        setKbItem(rows[n]);
        closeSubsOutside(rows[n]);
      };
      // → (or Enter) on a parent row: the first reveals its flyout, the second enters it.
      const openOrEnter = () => {
        const sub = subOf(kbItem);
        if (!sub) return false;
        if (sub.classList.contains('ctx-sub-visible')) kbEnterSub(sub);
        else kbOpenSub(kbItem);
        return true;
      };
      switch (e.key) {
        case 'ArrowDown': step(1); break;
        case 'ArrowUp': step(-1); break;
        case 'Tab': step(e.shiftKey ? -1 : 1); break;
        case 'Home': if (rows.length) { setKbItem(rows[0]); closeSubsOutside(rows[0]); } break;
        case 'End': if (rows.length) { setKbItem(rows.at(-1)); closeSubsOutside(rows.at(-1)); } break;
        case 'ArrowRight': if (kbItem && idx >= 0) openOrEnter(); break;
        case 'ArrowLeft': kbCloseSub(); break;
        default:   // Enter / Space
          if (!kbItem || idx < 0) break;
          if (!openOrEnter()) kbItem.click();
      }
    }, true);

  // The pointer, the open and the close paths clear the highlight too (contextMenu.js).
  return { setKbItem };
};
