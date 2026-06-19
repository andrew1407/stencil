import { ACCENTS, accentHex } from '../core/accents.js';

// Custom "main theme" dropdown: a trigger showing the current colour swatch +
// name, and a popup listbox where every option is a colour RECT next to its name.
// A native <select> can't paint a per-option swatch on every OS (notably macOS),
// so this small listbox does it explicitly. Calls onSelect(key) on choice and
// returns { set(key) } to re-sync the trigger when the value changes elsewhere.
export function buildAccentPicker(mount, { current, onSelect }) {
  const labelOf = (k) => (ACCENTS.find((a) => a.key === k) || ACCENTS[0]).label;
  let value = current;

  mount.classList.add('accent-dd');
  mount.innerHTML = `
    <button type="button" class="accent-dd-trigger" aria-haspopup="listbox" aria-expanded="false">
      <span class="accent-swatch js-cur-sw"></span>
      <span class="accent-dd-name js-cur-name"></span>
      <span class="accent-dd-caret" aria-hidden="true">▾</span>
    </button>
    <ul class="accent-dd-menu" role="listbox" hidden></ul>`;
  const trigger = mount.querySelector('.accent-dd-trigger');
  const menu = mount.querySelector('.accent-dd-menu');
  const curSw = mount.querySelector('.js-cur-sw');
  const curName = mount.querySelector('.js-cur-name');

  for (const a of ACCENTS) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    li.innerHTML =
      `<span class="accent-swatch" style="background:${a.hex}"></span>` +
      `<span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => choose(a.key));
    menu.appendChild(li);
  }

  const syncTrigger = () => {
    curSw.style.background = accentHex(value);
    curName.textContent = labelOf(value);
    for (const li of menu.children)
      li.setAttribute('aria-selected', li.dataset.key === value ? 'true' : 'false');
  };
  const onDocClick = (e) => { if (!mount.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') { close(); trigger.focus(); } };
  const open = () => {
    menu.hidden = false;
    trigger.setAttribute('aria-expanded', 'true');
    document.addEventListener('pointerdown', onDocClick, true);
    document.addEventListener('keydown', onKey);
  };
  const close = () => {
    menu.hidden = true;
    trigger.setAttribute('aria-expanded', 'false');
    document.removeEventListener('pointerdown', onDocClick, true);
    document.removeEventListener('keydown', onKey);
  };
  const choose = (key) => { value = key; syncTrigger(); close(); onSelect?.(key); };

  trigger.addEventListener('click', (e) => { e.preventDefault(); menu.hidden ? open() : close(); });
  syncTrigger();
  return { set: (k) => { value = k; syncTrigger(); } };
}
