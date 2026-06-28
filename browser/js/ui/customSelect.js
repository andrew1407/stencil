import { icon } from './icons.js';

// Custom dropdown overlaying a native <select> (kept as the source of truth) — macOS
// centers the native popup uncss-ably, so the compact toolbar selects look misplaced.
// Reuses the .accent-dd styling; the .value setter is wrapped to re-sync the trigger on
// programmatic sets, and user picks dispatch a bubbling `change` so existing handlers fire.
export function enhanceSelect(selectEl) {
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

  const buildOptions = () => {
    menu.innerHTML = '';
    for (const opt of selectEl.options) {
      const li = document.createElement('li');
      li.className = 'accent-dd-opt';
      li.setAttribute('role', 'option');
      li.dataset.value = opt.value;
      li.textContent = opt.textContent;
      li.addEventListener('click', () => choose(opt.value));
      menu.appendChild(li);
    }
  };

  const sync = () => {
    cur.textContent = labelOf(selectEl.value);
    for (const li of menu.children)
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
