import { getSettings, setSettings, DEFAULT_EDITOR_URL } from '../lib/stencil.js';

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
