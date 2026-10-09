import { rowMatches } from '../base.js';
import { showMenu, hideMenu, wireDragPick } from './dropdownMenu.js';
import { attachMenuScrollbar } from './menuScrollbar.js';
import { markSwap } from '../motion.js';
import { buildSelectFace } from './customSelectFace.js';
import { wireAltPeek } from '../tip/altPeek.js';
import { isTypingInFocus } from '../../utils.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

// `st` is one select's open-menu state (enhanceSelect builds it): its parts and options, the
// search rows, the hover-preview timer and the close in flight, plus the callbacks below use.
const applySearch = (st) => {
  const q = st.searchInput ? st.searchInput.value : '';
  let any = false;
  for (const li of st.menu.querySelectorAll('.accent-dd-opt')) {
    const show = rowMatches(`${li.textContent} ${li.dataset.value}`, q);
    li.style.display = show ? '' : 'none';
    if (show) any = true;
  }
  if (st.noMatchRow) st.noMatchRow.style.display = any ? 'none' : '';
};

// Rebuilt on every open; filtering only toggles row display.
const buildOptions = (st) => {
  const { menu, selectEl, icons, preview } = st;
  menu.innerHTML = '';
  if (st.search) {
    const row = document.createElement('li');
    row.className = 'accent-dd-search-row';
    st.searchInput = document.createElement('input');
    st.searchInput.type = 'text';
    st.searchInput.className = 'accent-dd-search';
    st.searchInput.placeholder = 'Search…';
    st.searchInput.addEventListener('input', () => applySearch(st));
    row.appendChild(st.searchInput);
    menu.appendChild(row);
  }
  for (const opt of selectEl.options) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.value = opt.value;
    const glyph = icons ? icons(opt.value) : '';
    if (glyph) {
      const slot = document.createElement('span');
      slot.className = 'cs-opt-icon';
      slot.innerHTML = glyph;
      const label = document.createElement('span');
      label.className = 'cs-opt-label';
      label.textContent = opt.textContent;
      li.append(slot, label);
    } else {
      li.textContent = opt.textContent;
    }
    li.addEventListener('click', () => st.choose(opt.value));
    // pointerleave fires once for the whole menu, so moving between rows just re-previews.
    if (preview) {
      li.addEventListener('pointerenter', () => {
        st.clearHover();
        st.hoverTimer = setTimeout(() => { st.hoverTimer = null; st.previewActive = true; preview(opt.value); }, st.hoverMs);
      });
      li.addEventListener('pointerleave', st.clearHover);
    }
    menu.appendChild(li);
  }
  if (st.search) {
    st.noMatchRow = document.createElement('li');
    st.noMatchRow.className = 'accent-dd-no-match';
    st.noMatchRow.textContent = 'No matching format.';
    st.noMatchRow.style.display = 'none';
    menu.appendChild(st.noMatchRow);
  }
};

const openMenu = (st) => {
  const { menu, trigger } = st;
  // Reopening mid-close: abort the exit, or its animationend would hide the fresh list.
  clearTimeout(st.closeTimer);
  if (st.closeDone) menu.removeEventListener('animationend', st.closeDone);
  menu.classList.remove('dd-closing');
  buildOptions(st);
  st.sync();
  // Placed in viewport space: `.controls` clips its overflow.
  showMenu(menu, trigger);
  attachMenuScrollbar(menu);   // only a list too tall for its cap draws a thumb
  if (st.searchInput) st.searchInput.focus();
  trigger.setAttribute('aria-expanded', 'true');
  document.addEventListener('pointerdown', st.onDocDown, true);
  document.addEventListener('keydown', st.onKey);
};

// hideMenu flies the list into the trigger (dropdownMenu.js surfaceOut) with no exit animation
// to wait on; `.dd-closing` only guards a reopen mid-flight.
const closeMenu = (st) => {
  const { menu, trigger } = st;
  if (menu.hidden || menu.classList.contains('dd-closing')) return;
  st.restorePreview();   // a menu dismissed without a pick reverts to the committed value
  st.peek.notifyClosed();
  trigger.setAttribute('aria-expanded', 'false');
  document.removeEventListener('pointerdown', st.onDocDown, true);
  document.removeEventListener('keydown', st.onKey);
  st.closeDone = (e) => {
    if (e && e.target !== menu) return;
    clearTimeout(st.closeTimer);
    menu.removeEventListener('animationend', st.closeDone);
    menu.classList.remove('dd-closing');
    hideMenu(menu);
  };
  st.closeDone();
};

// Custom dropdown overlaying a native <select> (kept as source of truth) — macOS cannot
// style the native popup, so the toolbar's compact selects looked misplaced there.
// `preview(value)` is a canvas-only repaint on hover; it never persists — only a real
// pick commits, through the native `change` event.
export function enhanceSelect(selectEl, { search = false, icons = null, preview = null } = {}) {
  if (!selectEl || selectEl.dataset.csEnhanced) return;
  selectEl.dataset.csEnhanced = '1';

  const labelOf = (val) => {
    const opt = [...selectEl.options].find((o) => o.value === val);
    return opt ? opt.textContent : '';
  };

  const { wrap, trigger, menu, cur, curIcon, syncDisabled, fitToWidestOption } = buildSelectFace(selectEl);

  // The committed value returns once the pointer leaves the list or the menu closes without a
  // pick; the preview waits for the pointer to settle, so skimming repaints nothing.
  const st = { selectEl, menu, trigger, search, icons, preview, hoverMs: constants.DEBOUNCE.previewHoverMs,
    searchInput: null, noMatchRow: null, hoverTimer: null, previewActive: false, closeTimer: null, closeDone: null };
  const clearHover = () => { if (st.hoverTimer) { clearTimeout(st.hoverTimer); st.hoverTimer = null; } };
  const restorePreview = () => {
    clearHover();
    if (!preview || !st.previewActive) return;
    st.previewActive = false;
    preview(selectEl.value);
  };
  if (preview) menu.addEventListener('pointerleave', restorePreview);

  // Only a REAL change flies (motion.js markSwap), not a first paint or a no-op set. `settled`
  // flips after the first frame, so boot's restore-then-wire counts as that first paint.
  let settled = false;
  if (typeof requestAnimationFrame === 'function') requestAnimationFrame(() => { settled = true; });
  else settled = true;

  let shown = null;
  const sync = () => {
    fitToWidestOption();
    const label = labelOf(selectEl.value);
    const changed = settled && shown !== null && label !== shown;
    if (!changed) cur.textContent = label;
    else markSwap(cur, () => { cur.textContent = label; });
    shown = label;
    // The glyph arrives with its own motion when the VALUE changed, like the label's swap:
    // .mm-play runs the hover keyframes once, then comes off so a hover can replay them.
    const glyph = icons ? icons(selectEl.value) : '';
    if (glyph !== curIcon.innerHTML) {
      curIcon.innerHTML = glyph;
      const svg = curIcon.firstElementChild;
      if (svg && changed) {
        svg.classList.add('mm-play');
        setTimeout(() => svg.classList.remove('mm-play'), 1800);
      }
    }
    for (const li of menu.querySelectorAll('.accent-dd-opt'))
      li.setAttribute('aria-selected', li.dataset.value === selectEl.value ? 'true' : 'false');
  };

  // Wrap .value so programmatic sets refresh the trigger (a native .value set does not
  // fire change, so we only re-sync the UI here, never dispatch).
  const proto = Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value');
  Object.defineProperty(selectEl, 'value', {
    configurable: true,
    get() { return proto.get.call(this); },
    set(v) {
      proto.set.call(this, v);
      sync();
    },
  });

  // The open menu lives on <body> (dropdownMenu.js), so it is NOT inside `wrap` — an
  // outside press has to miss both, or picking an option would close before the click.
  const onDocDown = (e) => {
    if (!wrap.contains(e.target) && !menu.contains(e.target)) close();
  };
  const onKey = (e) => {
    if (e.key === 'Escape') {
      close();
      trigger.focus();
    }
  };
  const open = () => openMenu(st);
  const close = () => closeMenu(st);
  // The pick applies first, so the exit and the trigger's swap play under the newly picked
  // value. The raw setter keeps the wrapped one's sync for after the dispatch.
  const choose = (v) => {
    clearHover();
    st.previewActive = false;   // the pick commits the real value; no revert on the close below
    proto.set.call(selectEl, v);
    selectEl.dispatchEvent(new Event('change', { bubbles: true }));
    close();
    sync();
  };

  trigger.addEventListener('click', (e) => {
    e.preventDefault();
    if (selectEl.disabled) return;
    menu.hidden ? open() : close();
  });
  const peek = wireAltPeek(wrap, menu, {
    open, close, isOpen: () => !menu.hidden, enabled: () => !selectEl.disabled,
    isTyping: isTypingInFocus,
  });
  wireDragPick(trigger, menu, { open, close, enabled: () => !selectEl.disabled });
  Object.assign(st, { sync, choose, clearHover, restorePreview, onDocDown, onKey, peek });
  sync();   // initial trigger label; the menu itself is (re)built on open()
  // Options filled in later (server list, open-in targets) leave no value to set on load;
  // watching the element covers that and the disabled flag, which no event reports either.
  if (typeof MutationObserver === 'function') {
    new MutationObserver(() => { syncDisabled(); sync(); })
      .observe(selectEl, { attributes: true, attributeFilter: ['disabled'], childList: true });
  }
}

// Every <select> in one pass; a second call is a no-op (enhanceSelect marks what it took
// over). `search` marks the long lists (e.g. page-size) where scrolling alone is slow.
export function enhanceAllSelects(root = document, { search = ['page-size'], preview = null } = {}) {
  for (const sel of root.querySelectorAll('select:not([data-cs-skip])'))
    enhanceSelect(sel, { search: search.includes(sel.id), preview: preview ? preview(sel) : null });
}
