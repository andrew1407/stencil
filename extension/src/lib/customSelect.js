// Custom <select> dropdown — PORT of browser/js/ui/customSelect.js, kept rule-for-rule.
// The native list is drawn by the OS, so no page style reaches it; this one is built
// from the SAME <select>, which stays the source of truth (wrapped `.value` setter, a
// bubbling `change` on pick, `hidden` mirrored onto the wrapper).

import { icon } from './icons.js';
import { showMenu, hideMenu } from './dropdownMenu.js';


// The browser's base.js rowMatches.
const rowMatches = (text, query) => {
  const q = String(query ?? '').trim().toLowerCase();
  return !q || String(text ?? '').toLowerCase().includes(q);
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
  // The trigger inherits the tip and name (data-title, never `title` — see lib/tip.js).
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

  // The preview waits for the pointer to settle, so skimming the rows previews nothing.
  const PREVIEW_HOVER_MS = 280;
  let previewActive = false;
  let hoverTimer = null;
  const clearHover = () => { if (hoverTimer) { clearTimeout(hoverTimer); hoverTimer = null; } };
  const restorePreview = () => {
    clearHover();
    if (!preview || !previewActive) return;
    previewActive = false;
    preview(selectEl.value);
  };
  if (preview) menu.addEventListener('pointerleave', restorePreview);

  // Rebuilt with the menu on every open; filtering only toggles row display.
  let searchInput = null;
  let noMatchRow = null;
  const applySearch = () => {
    const q = searchInput ? searchInput.value : '';
    let any = false;
    for (const li of menu.querySelectorAll('.accent-dd-opt')) {
      const show = rowMatches(`${li.textContent} ${li.dataset.value}`, q);
      li.style.display = show ? '' : 'none';
      if (show) any = true;
    }
    if (noMatchRow) noMatchRow.style.display = any ? 'none' : '';
  };

  const buildOptions = () => {
    menu.innerHTML = '';
    if (search) {
      const row = document.createElement('li');
      row.className = 'accent-dd-search-row';
      searchInput = document.createElement('input');
      searchInput.type = 'text';
      searchInput.className = 'accent-dd-search';
      searchInput.placeholder = 'Search…';
      searchInput.addEventListener('input', applySearch);
      row.appendChild(searchInput);
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
      li.addEventListener('click', () => choose(opt.value));
      if (preview) {
        li.addEventListener('pointerenter', () => {
          clearHover();
          hoverTimer = setTimeout(() => { hoverTimer = null; previewActive = true; preview(opt.value); }, PREVIEW_HOVER_MS);
        });
        li.addEventListener('pointerleave', clearHover);
      }
      menu.appendChild(li);
    }
    if (search) {
      noMatchRow = document.createElement('li');
      noMatchRow.className = 'accent-dd-no-match';
      noMatchRow.textContent = 'No matching format.';
      noMatchRow.style.display = 'none';
      menu.appendChild(noMatchRow);
    }
  };

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
  const open = () => {
    // Reopening mid-close aborts the exit, or its animationend would hide the fresh list.
    clearTimeout(closeTimer);
    if (closeDone) menu.removeEventListener('animationend', closeDone);
    menu.classList.remove('dd-closing');
    buildOptions();
    sync();
    // Placed in viewport space: `.controls` clips its overflow.
    showMenu(menu, trigger);
    if (searchInput) searchInput.focus();
    trigger.setAttribute('aria-expanded', 'true');
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  // hideMenu flies the list back into the trigger at once; the `.dd-closing` clean-up
  // stays only so a reopen mid-flight starts from a clean slate.
  let closeTimer = null;
  let closeDone = null;
  const close = () => {
    if (menu.hidden || menu.classList.contains('dd-closing')) return;
    restorePreview();
    trigger.setAttribute('aria-expanded', 'false');
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey);
    closeDone = (e) => {
      if (e && e.target !== menu) return;
      clearTimeout(closeTimer);
      menu.removeEventListener('animationend', closeDone);
      menu.classList.remove('dd-closing');
      hideMenu(menu);
    };
    closeDone();
  };
  // The pick is APPLIED first, so the exit plays under what was just picked (None → Fire
  // must not arrive with None's no-motion).
  const choose = (v) => {
    clearHover();
    previewActive = false;
    proto.set.call(selectEl, v);
    selectEl.dispatchEvent(new Event('change', { bubbles: true }));
    close();
    sync();
  };

  trigger.addEventListener('click', (e) => {
    e.preventDefault();
    menu.hidden ? open() : close();
  });
  sync();
}
