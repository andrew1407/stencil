import { getSettings, setSettings, DEFAULT_EDITOR_URL } from '../lib/stencil.js';

// Theme accent — persisted separately in localStorage (window.StencilAccent, set
// up by lib/accent.js) so it applies flash-free across the extension's pages. It
// is independent of the Save button: changing it applies + persists instantly.
const accent = window.StencilAccent;
if (accent) {
  // Custom colour-swatch dropdown — a colour RECT + name per option, since a
  // native <select> can't paint per-option swatches on every OS (e.g. macOS).
  const mount = document.getElementById('accent');
  const meta = (key) => accent.list.find((a) => a.key === key) || accent.list[0];
  let value = accent.get();

  mount.innerHTML =
    '<button type="button" class="accent-dd-trigger" aria-haspopup="listbox" aria-expanded="false">' +
    '<span class="accent-swatch js-cur-sw"></span><span class="accent-dd-name js-cur-name"></span>' +
    '<span class="accent-dd-caret" aria-hidden="true">▾</span></button>' +
    '<ul class="accent-dd-menu" role="listbox" hidden></ul>';
  const trigger = mount.querySelector('.accent-dd-trigger');
  const menu = mount.querySelector('.accent-dd-menu');
  const curSw = mount.querySelector('.js-cur-sw');
  const curName = mount.querySelector('.js-cur-name');

  for (const a of accent.list) {
    const li = document.createElement('li');
    li.className = 'accent-dd-opt';
    li.setAttribute('role', 'option');
    li.dataset.key = a.key;
    li.innerHTML = `<span class="accent-swatch" style="background:${a.hex}"></span><span class="accent-dd-name">${a.label}</span>`;
    li.addEventListener('click', () => choose(a.key));
    menu.appendChild(li);
  }

  const sync = () => {
    const m = meta(value);
    curSw.style.background = m.hex;
    curName.textContent = m.label;
    for (const li of menu.children)
      li.setAttribute('aria-selected', li.dataset.key === value ? 'true' : 'false');
  };
  const onDocPtr = (e) => { if (!mount.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') close(); };
  const open = () => { menu.hidden = false; trigger.setAttribute('aria-expanded', 'true'); document.addEventListener('pointerdown', onDocPtr, true); document.addEventListener('keydown', onKey); };
  const close = () => { menu.hidden = true; trigger.setAttribute('aria-expanded', 'false'); document.removeEventListener('pointerdown', onDocPtr, true); document.removeEventListener('keydown', onKey); };
  const choose = (key) => { value = accent.set(key); sync(); close(); };

  trigger.addEventListener('click', () => { menu.hidden ? open() : close(); });
  sync();
}

(async () => {
  const { editorUrl, page, markOpened, openedFirst, exposeWindowStencil } = await getSettings();
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('page').value = page;
  document.getElementById('markOpened').checked = markOpened;
  document.getElementById('openedFirst').checked = openedFirst;
  document.getElementById('exposeWindowStencil').checked = exposeWindowStencil;
})();

document.getElementById('save').addEventListener('click', async () => {
  const editorUrl = (document.getElementById('editorUrl').value || '').trim() || DEFAULT_EDITOR_URL;
  await setSettings({ editorUrl, page: document.getElementById('page').value, markOpened: document.getElementById('markOpened').checked, openedFirst: document.getElementById('openedFirst').checked, exposeWindowStencil: document.getElementById('exposeWindowStencil').checked });
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('status').textContent = '✓ Saved';
  setTimeout(() => { document.getElementById('status').textContent = ''; }, 1500);
});
