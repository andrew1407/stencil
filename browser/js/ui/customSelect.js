import { rowMatches } from './base.js';
import { showMenu, hideMenu } from './dropdownMenu.js';
import { markSwap } from './motion.js';
import { buildSelectFace } from './customSelectFace.js';

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

  // Search state, rebuilt with the menu on every open; filtering only toggles row display.
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
      // Hovering a row previews it (see `preview` above); pointerleave fires once for the
      // whole menu, so moving between rows just re-previews.
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
  const open = () => {
    // Reopening mid-close: abort the exit, or its animationend would hide the fresh list.
    clearTimeout(closeTimer);
    if (closeDone) menu.removeEventListener('animationend', closeDone);
    menu.classList.remove('dd-closing');
    buildOptions();
    sync();
    // Placed against the trigger in viewport space: `.controls` clips its overflow and
    // the toolbar sits low enough that a long list would run off the window.
    showMenu(menu, trigger);
    if (searchInput) searchInput.focus();
    trigger.setAttribute('aria-expanded', 'true');
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  // hideMenu flies the list into the trigger (dropdownMenu.js surfaceOut) with no exit animation
  // to wait on; `.dd-closing` only guards a reopen mid-flight.
  let closeTimer = null;
  let closeDone = null;
  const close = () => {
    if (menu.hidden || menu.classList.contains('dd-closing')) return;
    restorePreview();   // a menu dismissed without a pick reverts to the committed value
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
  // The pick applies first, so the exit and the trigger's swap play under the newly picked
  // value. The raw setter keeps the wrapped one's sync for after the dispatch.
  const choose = (v) => {
    clearHover();
    previewActive = false;   // the pick commits the real value; no revert on the close below
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
