import { icon } from './icons.js';
import { rowMatches, escapeHtml } from './base.js';
import { showMenu, hideMenu } from './dropdownMenu.js';
import { markSwap, pinWidestFace } from './motion.js';

// Custom dropdown overlaying a native <select> (kept as the source of truth) — macOS
// centers the native popup uncss-ably, so the compact toolbar selects look misplaced.
// Reuses the .accent-dd styling; the .value setter is wrapped to re-sync the trigger on
// programmatic sets, and user picks dispatch a bubbling `change` so existing handlers fire.
// `search: true` (page-size) pins a .modal-search-style input at the top of the popup:
// auto-focused on open, it filters the option rows (label + value, case-insensitive
// substring via rowMatches) with a "no match" placeholder; Escape still closes.
// `icons(value)` answers an inline-SVG string for a row (or ''), shown before the label
// and in the trigger — the motion modes' glyphs (motionIcons.js) are the one user today.
// `preview(value)` — optional: called with an option's value while the pointer rests on
// its row, so a filter or compare mode is live-applied to the canvas as a preview; called
// again with the committed value (the one the native select still holds) when the pointer
// leaves the list or the menu closes without a pick. It must NOT persist — it is a repaint
// only (settingsController.preview). A real pick commits through the normal `change` path.
export function enhanceSelect(selectEl, { search = false, icons = null, preview = null } = {}) {
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
  // The trigger IS the control now, so it inherits the hover text the native select
  // carried — the rich tooltip attributes included (data-title's bullet list, the
  // disabled-reason line, the hotkey keycap), or an enhanced control would go silent.
  if (selectEl.dataset.title) trigger.dataset.title = selectEl.dataset.title;
  for (const k of ['title', 'disabledReason', 'hkTitle'])
    if (selectEl.dataset[k] != null) trigger.dataset[k] = selectEl.dataset[k];
  // …and its enabled state: a disabled <select> is hidden here, so nothing would have
  // shown that image-filter / compare are dead until an image is loaded.
  const syncDisabled = () => {
    trigger.disabled = selectEl.disabled;
    trigger.classList.toggle('cs-disabled', selectEl.disabled);
  };
  syncDisabled();
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

  // Hover-preview bookkeeping: put the committed value back (selectEl.value — a preview
  // never touches it) once the pointer has left the list or the menu closed without a
  // pick. The preview waits for the pointer to settle, so skimming the rows repaints
  // nothing; moving off cancels a pending one.
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
      // Hovering a row previews it on the canvas (see `preview` above); leaving the list
      // puts the committed value back. pointerleave fires once, on the way out of the
      // whole menu, so moving between rows just re-previews the new one.
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

  // The chosen word is a mark like any other (motion.js markSwap): the outgoing value
  // comes apart into motes and the incoming one forms out of them, in place. Only a real
  // change flies — the first paint, a re-sync on open and a no-op set write straight
  // through, or every dropdown would deal itself in on open.
  // The box stays put because the label is floored at its WIDEST option, not the one
  // showing, so a row of selects doesn't shuffle as you use them. The app's own face pin
  // does the measuring — in the real element, so the true font, padding and border count,
  // and re-measured once webfonts settle. A FLOOR (min-width), not a pin: an option list
  // that later outgrows the cap should still stretch the control rather than clip.
  // Capped so one very long server URL can't overrun its row.
  const MAX_FIT_PX = 240;
  let fittedCount = -1;
  const fitToWidestOption = () => {
    if (fittedCount === selectEl.options.length || !selectEl.options.length) return;
    const faces = [...selectEl.options].map((o) => escapeHtml(o.textContent));
    // Only a measurement that MEANT something counts as done: a select enhanced inside a
    // window that is still display:none measures zero, and must fit again once it is up.
    if (pinWidestFace(cur, faces, { force: true, prop: 'minWidth', max: MAX_FIT_PX }) > 0)
      fittedCount = selectEl.options.length;
  };

  let shown = null;
  const sync = () => {
    fitToWidestOption();
    const label = labelOf(selectEl.value);
    const changed = shown !== null && label !== shown;
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
  // Closing hands over to the dust: hideMenu measures the list where it stands and
  // flies a cloud of it back into the trigger (ui/dropdownMenu.js surfaceOut), so the
  // list itself goes at once and there is no exit animation to wait on. The
  // `.dd-closing` clean-up stays only so a reopen mid-flight starts from a clean slate.
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
  // The pick is APPLIED first, so the exit and the trigger's swap play under what was just
  // picked (None → Fire used to arrive with None's no-motion — user report). The raw setter
  // keeps the wrapped one's sync for after the dispatch.
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
  // A select whose OPTIONS are filled in later (the server list, the open-in targets) has
  // no value to set — its trigger would sit on the empty label it was born with. Watching
  // the element covers both that and the disabled flag, which no event reports either.
  if (typeof MutationObserver === 'function') {
    new MutationObserver(() => { syncDisabled(); sync(); })
      .observe(selectEl, { attributes: true, attributeFilter: ['disabled'], childList: true });
  }
}

// Every <select> the app has, in one pass: they all wear the same dropdown, and a
// second call is a no-op (enhanceSelect marks what it has taken over). `search` is for
// the long lists — the ISO page formats — where scrolling alone is too slow.
export function enhanceAllSelects(root = document, { search = ['page-size'], preview = null } = {}) {
  for (const sel of root.querySelectorAll('select:not([data-cs-skip])'))
    enhanceSelect(sel, { search: search.includes(sel.id), preview: preview ? preview(sel) : null });
}
