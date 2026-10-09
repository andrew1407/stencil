// Custom <select> dropdown, derived from browser/js/ui/control/customSelect.js (its menu helpers pinned).
// The OS draws the native list, so no page style reaches it; this one is built from the SAME <select>,
// the source of truth (wrapped `.value` setter, a bubbling `change` on pick, `hidden` mirrored).
import { icon } from '../icons.js';
import { showMenu, hideMenu, wireDragPick } from './dropdownMenu.js';
import { attachMenuScrollbar } from './menuScrollbar.js';
import { wireAltPeek } from '../tip/altPeek.js';

// The browser's base.js rowMatches.
const rowMatches = (text, query) => {
  const q = String(query ?? '').trim().toLowerCase();
  return !q || String(text ?? '').toLowerCase().includes(q);
};

// `st`: one select's open-menu state and the callbacks these use, built by enhanceSelect.
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

// hideMenu flies the list back at once; `.dd-closing` only guards a reopen mid-flight.
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

// `icons(value)` answers an inline-SVG string for a row (or ''); `preview(value)` is
// called while the pointer rests on a row and again with the committed value on leave.
export function enhanceSelect(selectEl, { search = false, icons = null, preview = null } = {}) {
  if (!selectEl || !selectEl.parentNode || selectEl.dataset.csEnhanced) return;
  selectEl.dataset.csEnhanced = '1';

  const labelOf = (val) => {
    const opt = [...selectEl.options].find((o) => o.value === val);
    return opt ? opt.textContent : '';
  };

  const wrap = document.createElement('span');
  wrap.className = 'accent-dd cs-dd';
  selectEl.parentNode.insertBefore(wrap, selectEl);
  wrap.appendChild(selectEl);
  selectEl.classList.add('cs-native');
  // The wrapper follows the native control's `hidden`, so the pages keep setting it.
  const syncHidden = () => { wrap.hidden = selectEl.hidden; };
  syncHidden();
  new MutationObserver(syncHidden).observe(selectEl, { attributes: true, attributeFilter: ['hidden'] });

  const trigger = document.createElement('button');
  trigger.type = 'button';
  trigger.className = 'accent-dd-trigger';
  trigger.setAttribute('aria-haspopup', 'listbox');
  trigger.setAttribute('aria-expanded', 'false');
  // The trigger inherits the tip and name (data-title, never `title` — see lib/tip/tip.js).
  const tip = selectEl.dataset ? selectEl.dataset.title : '';
  if (tip) trigger.setAttribute('data-title', tip);
  const aria = selectEl.getAttribute('aria-label');
  if (aria) trigger.setAttribute('aria-label', aria);
  trigger.innerHTML =
    '<span class="cs-cur-icon" aria-hidden="true"></span>' +
    '<span class="accent-dd-name cs-cur"></span>' +
    `<span class="accent-dd-caret" aria-hidden="true">${icon('chevron-down', { size: 13 })}</span>`;
  const menu = document.createElement('ul');
  menu.className = 'accent-dd-menu';
  menu.setAttribute('role', 'listbox');
  menu.hidden = true;
  wrap.append(trigger, menu);
  const cur = trigger.querySelector('.cs-cur');
  const curIcon = trigger.querySelector('.cs-cur-icon');

  const st = { selectEl, menu, trigger, search, icons, preview, hoverMs: 280,   // ms: skimming previews nothing
    searchInput: null, noMatchRow: null, hoverTimer: null, previewActive: false, closeTimer: null, closeDone: null };
  const clearHover = () => { if (st.hoverTimer) { clearTimeout(st.hoverTimer); st.hoverTimer = null; } };
  const restorePreview = () => {
    clearHover();
    if (!preview || !st.previewActive) return;
    st.previewActive = false;
    preview(selectEl.value);
  };
  if (preview) menu.addEventListener('pointerleave', restorePreview);

  let shown = null;   // null before the first paint
  const sync = () => {
    const label = labelOf(selectEl.value);
    const changed = shown !== null && label !== shown;
    cur.textContent = label;
    shown = label;
    for (const li of menu.querySelectorAll('.accent-dd-opt'))
      li.setAttribute('aria-selected', li.dataset.value === selectEl.value ? 'true' : 'false');
    // .mm-play runs the hover keyframes once on a VALUE change, then comes off.
    const glyph = icons ? icons(selectEl.value) : '';
    if (glyph !== curIcon.innerHTML) {
      curIcon.innerHTML = glyph;
      const svg = curIcon.firstElementChild;
      if (svg && changed) {
        svg.classList.add('mm-play');
        setTimeout(() => svg.classList.remove('mm-play'), 1800);
      }
    }
  };

  // A native .value set fires no change: re-sync the trigger, never dispatch.
  const proto = Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value');
  Object.defineProperty(selectEl, 'value', {
    configurable: true,
    get() { return proto.get.call(this); },
    set(v) {
      proto.set.call(this, v);
      sync();
    },
  });

  // The open menu lives on <body>, NOT inside `wrap`: an outside press must miss both.
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
  // The pick is APPLIED first, so the exit plays under what was just picked (None → Fire too).
  const choose = (v) => {
    clearHover();
    st.previewActive = false;
    proto.set.call(selectEl, v);
    selectEl.dispatchEvent(new Event('change', { bubbles: true }));
    close();
    sync();
  };

  trigger.addEventListener('click', (e) => {
    e.preventDefault();
    menu.hidden ? open() : close();
  });
  const peek = wireAltPeek(wrap, menu, { open, close, enabled: () => !selectEl.disabled });
  wireDragPick(trigger, menu, { open, close, enabled: () => !selectEl.disabled });
  Object.assign(st, { sync, choose, clearHover, restorePreview, onDocDown, onKey, peek });
  sync();
}
