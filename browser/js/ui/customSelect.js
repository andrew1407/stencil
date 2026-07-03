import { icon } from './icons.js';
import { rowMatches } from './base.js';

// Custom dropdown overlaying a native <select> (kept as the source of truth) — macOS
// centers the native popup uncss-ably, so the compact toolbar selects look misplaced.
// Reuses the .accent-dd styling; the .value setter is wrapped to re-sync the trigger on
// programmatic sets, and user picks dispatch a bubbling `change` so existing handlers fire.
// `search: true` (page-size) pins a .modal-search-style input at the top of the popup:
// auto-focused on open, it filters the option rows (label + value, case-insensitive
// substring via rowMatches) with a "no match" placeholder; Escape still closes.
export function enhanceSelect(selectEl, { search = false } = {}) {
  if (!selectEl || selectEl.dataset.csEnhanced) return;
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

  const trigger = document.createElement('button');
  trigger.type = 'button';
  trigger.className = 'accent-dd-trigger';
  trigger.setAttribute('aria-haspopup', 'listbox');
  trigger.setAttribute('aria-expanded', 'false');
  if (selectEl.title) trigger.title = selectEl.title;
  trigger.innerHTML =
    '<span class="accent-dd-name cs-cur"></span>' +
    `<span class="accent-dd-caret" aria-hidden="true">${icon('chevron-down', { size: 13 })}</span>`;
  const menu = document.createElement('ul');
  menu.className = 'accent-dd-menu';
  menu.setAttribute('role', 'listbox');
  menu.hidden = true;
  wrap.append(trigger, menu);
  const cur = trigger.querySelector('.cs-cur');

  // Search state — rebuilt with the menu on every open (so each open starts with an
  // empty query and every row visible). Filtering only toggles row display; the native
  // select, choose()/sync() and the outside-click close are untouched by it.
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
      li.textContent = opt.textContent;
      li.addEventListener('click', () => choose(opt.value));
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

  const sync = () => {
    cur.textContent = labelOf(selectEl.value);
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

  const onDocDown = (e) => {
    if (!wrap.contains(e.target)) close();
  };
  const onKey = (e) => {
    if (e.key === 'Escape') {
      close();
      trigger.focus();
    }
  };
  const open = () => {
    buildOptions();
    sync();
    menu.hidden = false;
    if (searchInput) searchInput.focus();
    trigger.setAttribute('aria-expanded', 'true');
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  const close = () => {
    menu.hidden = true;
    trigger.setAttribute('aria-expanded', 'false');
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey);
  };
  const choose = (v) => {
    selectEl.value = v;   // routes through the wrapped setter → re-syncs the trigger
    close();
    selectEl.dispatchEvent(new Event('change', { bubbles: true }));
  };

  trigger.addEventListener('click', (e) => {
    e.preventDefault();
    menu.hidden ? open() : close();
  });
  sync();   // initial trigger label; the menu itself is (re)built on open()
}
