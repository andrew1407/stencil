import { getSettings, setSettings, DEFAULT_EDITOR_URL } from '../lib/stencil.js';

(async () => {
  const { editorUrl, page } = await getSettings();
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('page').value = page;
})();

document.getElementById('save').addEventListener('click', async () => {
  const editorUrl = (document.getElementById('editorUrl').value || '').trim() || DEFAULT_EDITOR_URL;
  await setSettings({ editorUrl, page: document.getElementById('page').value });
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('status').textContent = '✓ Saved';
  setTimeout(() => { document.getElementById('status').textContent = ''; }, 1500);
});
