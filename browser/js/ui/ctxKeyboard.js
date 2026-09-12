import { isTypingTarget } from '../utils.js';

// The context menu's keyboard navigation (desktop QMenu parity). The hover path owns the
// same activeSub state, read and written through the host. ctxKeyStep: the row `step`
// away from `idx` among `count` rows, wrapping; no row yet ⇒ the first (down) or last (up).
export const ctxKeyStep = (count, idx, step) => {
  if (!count) return -1;
  if (idx < 0) return step > 0 ? 0 : count - 1;
  return (idx + step + count) % count;
};
export const CTX_NAV_KEYS = Object.freeze(['ArrowDown', 'ArrowUp', 'ArrowLeft', 'ArrowRight', 'Enter', ' ', 'Home', 'End', 'Tab']);
// What Tab walks inside an open flyout: its real form controls, in markup order, skipping hidden ones.
const CTX_FOCUSABLE = 'input, select, textarea, button, [tabindex]:not([tabindex="-1"])';
export const ctxFocusables = (level) => [...level.querySelectorAll(CTX_FOCUSABLE)]
  .filter((el) => !el.disabled && el.getClientRects().length > 0);

export const wireCtxKeyboard = ({ menu, menuIsOpen, chatRowMenuOpen, closeSub, positionSub,
                                 activeSub, setActiveSub }) => {
    // ↑/↓ walk the deepest open level, → opens the row's flyout, ← closes it, Enter/Space
    // picks. The highlighted row wears .ctx-kb (components/contextMenu.css); a real pointer
    // move hands the highlight back to :hover.
    let kbItem = null;
    const setKbItem = (item) => {
      if (kbItem && kbItem !== item) kbItem.classList.remove('ctx-kb');
      kbItem = item || null;
      if (kbItem) {
        kbItem.classList.add('ctx-kb');
        kbItem.scrollIntoView?.({ block: 'nearest' });
      }
    };
    const kbRows = (level) => [...level.querySelectorAll(':scope > .ctx-item')]
      .filter(i => i.style.display !== 'none');
    // The level holding the highlighted row, else the deepest open flyout.
    const kbLevel = () => kbItem ? kbItem.parentElement
      : (activeSub()?.classList.contains('ctx-sub-visible')) ? activeSub() : menu;
    const subOf = (item) => item?.querySelector(':scope > .ctx-sub') || null;
    // Where the second → lands: the flyout's text box where it has one, else its first control.
    const primaryControl = (sub) => {
      const controls = ctxFocusables(sub);
      return controls.find((el) => el.tagName === 'TEXTAREA') || controls[0] || null;
    };
    const kbEnterSub = (sub) => {
      const control = primaryControl(sub);
      if (control) { control.focus(); setKbItem(null); return; }
      setKbItem(kbRows(sub)[0] || null);
    };
    const kbOpenSub = (item) => {
      const sub = subOf(item);
      if (!sub || item.dataset.noSub === '1') return false;
      document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(s => {
        if (s !== sub && !s.contains(item)) closeSub(s);
      });
      document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => {
        if (i !== item && !i.contains(item)) i.classList.remove('ctx-open-sub');
      });
      positionSub(item, sub);
      setActiveSub(sub, item);
      setKbItem(item);   // revealed only — a second → enters it
      return true;
    };
    // Close the deepest open flyout; the highlight lands on its parent row unless `keepHighlight`.
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
    // Walking off a parent row folds its flyout (Qt parity).
    const closeSubsOutside = (item) => {
      while (activeSub() && !activeSub().contains(item) && activeSub().__ctxItem !== item) {
        if (!kbCloseSub(true)) break;
      }
    };
    // Capture phase, consumed only while open: the arrows must never reach the canvas
    // pan/nudge binding, whose scroll would close the menu.
    document.addEventListener('keydown', e => {
      if (!menuIsOpen() || chatRowMenuOpen()) return;
      if (!CTX_NAV_KEYS.includes(e.key)) return;
      const open = activeSub();
    const openSub = (open && open.classList.contains('ctx-sub-visible')) ? open : null;
      // Tab walks the open flyout's own controls, wrapping; a level with no controls treats
      // Tab as the arrows.
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
      // A control inside the flyout has the keys; only ← leaves it, except in a text field
      // where ← moves the caret (Tab / Shift+Tab leave those).
      if (openSub && openSub.contains(document.activeElement) && document.activeElement !== openSub) {
        // A text field keeps every key, Enter included (the chat sends on it).
        if (isTypingTarget(e.target)) return;
        if (e.key.startsWith('Arrow')) e.stopPropagation();
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

  return { setKbItem };
};
