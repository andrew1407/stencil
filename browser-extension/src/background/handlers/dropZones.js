// The panel's row drag injects a 4-quadrant overlay on the target tab; a drop in one
// quadrant comes back here and reuses the same hand-off machinery as the page relays.
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, buildHandoff } from '../../lib/stencil.js';
import { mountDropZones, unmountDropZones } from '../../lib/drop/dropZones.js';
import { mountDropChoice } from '../../lib/drop/dropChoice.js';
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from '../../lib/highlight/highlightColor.js';
import { THEME_STORAGE_KEY, RESOLVED_STORAGE_KEY, THEME_MODES, injectedScheme } from '../../lib/prefs/shellTheme.js';
import { recordOpened } from '../../lib/prefs/ledger.js';
import { MSG } from '../../lib/messages.js';
import { askEditorTab } from '../editorRelay.js';

export const dropZoneHandlers = {
  // Inject the on-page 4-quadrant drop overlay, tinted to the theme accent.
  [MSG.DROPZONES_ARM]: (msg) => {
    if (msg.tabId == null) return;
    (async () => {
      let accent = DEFAULT_HL;
      // 'system' resolved by the extension's own pages, not by the landing page: the two
      // can disagree, and the overlay must match the panel the drag started in.
      let mode = 'system';
      try {
        const l = await chrome.storage.local.get([ACCENT_STORAGE_KEY, THEME_STORAGE_KEY, RESOLVED_STORAGE_KEY]);
        accent = ACCENT_HEX[l[ACCENT_STORAGE_KEY]] || DEFAULT_HL;
        if (THEME_MODES.includes(l[THEME_STORAGE_KEY])) mode = l[THEME_STORAGE_KEY];
        mode = injectedScheme(mode, l[RESOLVED_STORAGE_KEY]);
      } catch { /* defaults */ }
      const probe = await askEditorTab(msg.tabId, { type: MSG.EDITOR_STATE, thumbnail: false });
      chrome.scripting.executeScript({ target: { tabId: msg.tabId }, world: 'ISOLATED', func: mountDropZones, args: [accent, !!probe.ok, mode] })
        .catch(() => { /* restricted page — no overlay */ });
    })();
  },
  // Backstop: it also self-removes on drop / leaving the window / Escape / timeout.
  [MSG.DROPZONES_DISARM]: (msg) => {
    if (msg.tabId == null) return;
    chrome.scripting.executeScript({ target: { tabId: msg.tabId }, world: 'ISOLATED', func: unmountDropZones })
      .catch(() => { /* restricted page — nothing to remove */ });
  },
  // A row dropped in a quadrant reuses the same hand-off machinery as the page API relays.
  [MSG.PAGE_DROP]: (msg, sender) => {
    (async () => {
      try {
        const { url, action } = msg;
        if (!url) return;
        if (action === 'newtab') { chrome.tabs.create({ url }); return; }
        const tabId = sender.tab?.id;
        const resource = sender.tab?.url || '';
        const name = filenameFromUrl(url);
        const { page, editorUrl } = await getSettings();
        // Editor-aware: a drop landing ON a live editor acts on THAT editor.
        const probe = tabId != null ? await askEditorTab(tabId, { type: MSG.EDITOR_STATE, thumbnail: false }) : { ok: false };
        if (probe.ok) {
          let mode = 'new';
          // Incognito never persists, so it needs no replace chooser.
          if (action !== 'incognito' && probe.state?.hasImage) {
            let accent = DEFAULT_HL;
            let scheme = 'system';
            try {
              const l = await chrome.storage.local.get([ACCENT_STORAGE_KEY, THEME_STORAGE_KEY, RESOLVED_STORAGE_KEY]);
              accent = ACCENT_HEX[l[ACCENT_STORAGE_KEY]] || DEFAULT_HL;
              scheme = injectedScheme(THEME_MODES.includes(l[THEME_STORAGE_KEY]) ? l[THEME_STORAGE_KEY] : 'system',
                                      l[RESOLVED_STORAGE_KEY]);
            } catch { /* defaults */ }
            const [res] = await chrome.scripting.executeScript({
              target: { tabId }, world: 'ISOLATED', func: mountDropChoice, args: [accent, scheme],
            }).catch(() => [null]);
            const choice = res?.result || 'cancel';
            if (choice === 'cancel') return;
            if (choice === 'newtab') {
              const dataUrl = await fetchAsDataUrl(url, { pageUrl: resource });
              await openEditorTab(buildHandoff({ name, source: url }, { dataUrl, page, resource, incognito: action === 'incognito' }));
              return;   // crop-in-a-new-tab has no editor to host the dialog — plain open
            }
            mode = 'replace';
          }
          const dataUrl = await fetchAsDataUrl(url, { pageUrl: resource });
          const payload = buildHandoff({ name, source: url }, { dataUrl, page, resource, incognito: action === 'incognito' });
          const reply = await askEditorTab(tabId, { type: MSG.EDITOR_IMPORT, payload, mode });
          if (!reply.ok) return;
          if (!payload.incognito) {
            await recordOpened({ source: url, resource, name, editorUrl }).catch(() => { /* badges just won't show */ });
          }
          if (action === 'crop') await askEditorTab(tabId, { type: MSG.EDITOR_CROP });
          return;
        }
        if (action === 'crop') {
          const src = await fetchAsDataUrl(url, { pageUrl: resource }).catch(() => url);   // video/non-image → let the crop page report it
          await launchCrop({ src, source: url, resource, tabId });
          return;
        }
        // here / incognito → open the editor with the image bytes.
        const dataUrl = await fetchAsDataUrl(url, { pageUrl: resource });
        const payload = buildHandoff({ name, source: url }, { dataUrl, page, resource, incognito: action === 'incognito' });
        if (action === 'incognito') await openEditorTab(payload);   // incognito = a fresh incognito editor tab
        else await launchEditorModal({ ...payload, tabId });        // here = in-page editor modal
      } catch (err) { console.warn('[stencil] page drop failed:', err?.message); }
    })();
  },
};
