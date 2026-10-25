// The saved settings form: loaded on open, written back by the Save button.
import { getSettings, setSettings } from '../lib/stencil.js';
import { isSharedOrigin } from '../lib/prefs/settings.js';
import { pageSizeOptions } from '../lib/image/cropGeometry.js';
import { icon } from '../lib/icons.js';

// On-page highlight colour: "theme" (follow the accent) or a custom hex.
const hlMode = document.getElementById('hl-mode');
const hlColor = document.getElementById('hl-color');
const hlCustomRow = document.getElementById('hl-custom-row');
const accentHex = () => { try { return window.StencilAccent.hexOf(window.StencilAccent.get()); } catch { return '#7c3aed'; } };
const syncHlCustomRow = () => { hlCustomRow.style.display = hlMode.value === 'custom' ? 'flex' : 'none'; };
hlMode.addEventListener('change', () => {
  // Seed the picker from the current accent the first time you switch to custom.
  if (hlMode.value === 'custom' && !hlColor.dataset.touched) hlColor.value = accentHex();
  syncHlCustomRow();
});
hlColor.addEventListener('input', () => { hlColor.dataset.touched = '1'; });

// The editor relay trusts the editor's whole origin, so a path-hosted editor shares it with its neighbours.
const editorUrlEl = document.getElementById('editorUrl');
const sharedNote = document.getElementById('editorUrlShared');
const syncSharedNote = () => {
  const url = editorUrlEl.value.trim();
  sharedNote.hidden = !isSharedOrigin(url);
  if (!sharedNote.hidden) {
    sharedNote.textContent = `⚠ ${new URL(url).origin} serves other pages too: with the editor page scripting API on, every page there can reach it.`;
  }
};
editorUrlEl.addEventListener('input', syncSharedNote);

// Every ISO A/B/C format from the shared table (canonical order); the stored value is the bare name.
document.getElementById('page').innerHTML = pageSizeOptions();

(async () => {
  const { editorUrl, editorUrlSet, shareUrl, shareUrlSet, page, markOpened, openedFirst, highlightColor, exposeWindowStencil, editorPageApiToggle, desktopScheme, telegramBotUsername } = await getSettings();
  // Empty until the user types one; the placeholder shows the default it falls back to.
  document.getElementById('editorUrl').value = editorUrlSet ? editorUrl : '';
  syncSharedNote();
  document.getElementById('shareUrl').value = shareUrlSet ? shareUrl : '';
  document.getElementById('page').value = page;
  document.getElementById('markOpened').checked = markOpened;
  document.getElementById('openedFirst').checked = openedFirst;
  document.getElementById('exposeWindowStencil').checked = exposeWindowStencil;
  document.getElementById('editorPageApi').checked = editorPageApiToggle;
  document.getElementById('desktopScheme').value = desktopScheme;
  document.getElementById('telegramBotUsername').value = telegramBotUsername;
  // A hex means custom; 'theme' (or anything else) means follow the accent.
  if (/^#[0-9a-f]{3,8}$/i.test(highlightColor)) { hlMode.value = 'custom'; hlColor.value = highlightColor; hlColor.dataset.touched = '1'; }
  else { hlMode.value = 'theme'; hlColor.value = accentHex(); }
  syncHlCustomRow();
})();

document.getElementById('save').addEventListener('click', async () => {
  const editorUrl = (document.getElementById('editorUrl').value || '').trim();
  const shareUrl = (document.getElementById('shareUrl').value || '').trim();
  const highlightColor = hlMode.value === 'custom' ? hlColor.value : 'theme';
  // Trim the "Open in…" operator config; a bare "@name" for the bot is tolerated.
  const desktopScheme = (document.getElementById('desktopScheme').value || '').trim();
  const telegramBotUsername = (document.getElementById('telegramBotUsername').value || '').trim().replace(/^@/, '');
  await setSettings({ editorUrl, shareUrl, page: document.getElementById('page').value, markOpened: document.getElementById('markOpened').checked, openedFirst: document.getElementById('openedFirst').checked, highlightColor, exposeWindowStencil: document.getElementById('exposeWindowStencil').checked, editorPageApi: document.getElementById('editorPageApi').checked, desktopScheme, telegramBotUsername });
  document.getElementById('telegramBotUsername').value = telegramBotUsername;
  document.getElementById('editorUrl').value = editorUrl;
  document.getElementById('status').innerHTML = icon('check', { size: 13 }) + ' Saved';
  setTimeout(() => { document.getElementById('status').textContent = ''; }, 1500);
});
