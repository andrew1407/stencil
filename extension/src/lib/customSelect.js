// ── Custom <select> dropdown ────────────────────────────────────────────────
// PORT of browser/js/ui/customSelect.js (the extension can't import across subprojects) —
// keep the two rule-for-rule.
//
// The reason it exists is the same on both sides, and it is the OS, not CSS: the LIST a
// native <select> opens is drawn by macOS, not by the page, so nothing the extension
// styles reaches it — it comes up centred on the control, in system type, ignoring the
// panel's theme entirely (in a small popup window it covers most of the page). This
// replaces that list with our own, built from the SAME <select>, which stays the source of
// truth: the wrapped `.value` setter re-syncs the trigger on programmatic sets, a user
// pick dispatches a bubbling `change`, and `hidden` is mirrored onto the wrapper — so
// every existing handler and every show/hide in the pages keeps working untouched.
//
// `search: true` (the page-size list) pins a filter input at the top of the popup.

import { icon } from './icons.js';
import { showMenu, hideMenu } from './dropdownMenu.js';

// Case-insensitive substring match, the browser's base.js rowMatches (not worth a module
// of its own here — this is its only caller in the extension).
const rowMatches = (text, query) => {
  const q = String(query ?? '').trim().toLowerCase();
  return !q || String(text ?? '').toLowerCase().includes(q);
};

export function enhanceSelect(selectEl, { search = false } = {}) {
  // Also guards the one select built at runtime (the popup's pin-target dialog): it asks
  // for this itself, and must not be enhanced before it is in the document.
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
  // Several of these selects are shown/hidden at runtime (`sel.hidden = …` in popup.js).
  // The native control is inside the wrapper now, so the wrapper is what has to disappear
  // with it — watched rather than wrapped, so the pages keep setting `hidden` as they do.
  const syncHidden = () => { wrap.hidden = selectEl.hidden; };
  syncHidden();
  new MutationObserver(syncHidden).observe(selectEl, { attributes: true, attributeFilter: ['hidden'] });

  const trigger = document.createElement('button');
  trigger.type = 'button';
  trigger.className = 'accent-dd-trigger';
  trigger.setAttribute('aria-haspopup', 'listbox');
  trigger.setAttribute('aria-expanded', 'false');
  // The hidden <select> keeps the description + accessible name; the visible trigger
  // inherits both (data-title, never `title` — see lib/tip.js).
  const tip = selectEl.dataset ? selectEl.dataset.title : '';
  if (tip) trigger.setAttribute('data-title', tip);
  const aria = selectEl.getAttribute('aria-label');
  if (aria) trigger.setAttribute('aria-label', aria);
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
  // Closing plays the entrance backwards — the list shrinks toward the corner it grew
  // from — and only then is it hidden and put back (animationend, with a timer fallback so
  // a neutralised or missing animation can never wedge it open). animationend BUBBLES, so
  // an option row's own hover transition must not be mistaken for the menu's exit.
  let closeTimer = null;
  let closeDone = null;
  const reducedMotion = () =>
    typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  const close = () => {
    if (menu.hidden || menu.classList.contains('dd-closing')) return;
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
    if (reducedMotion()) { closeDone(); return; }
    menu.classList.add('dd-closing');
    closeTimer = setTimeout(closeDone, 250);
    menu.addEventListener('animationend', closeDone);
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
