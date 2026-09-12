// ── On-page drop zones: arm, disarm, and the drop itself ────────────────────
// The panel's row drag injects a 4-quadrant overlay on the target tab; a drop in one
// quadrant comes back here and reuses the same hand-off machinery as the page relays.
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, buildHandoff } from '../../lib/stencil.js';
import { mountDropZones, unmountDropZones } from '../../lib/dropZones.js';
import { mountDropChoice } from '../../lib/dropChoice.js';
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from '../../lib/highlightColor.js';
import { THEME_STORAGE_KEY, THEME_MODES } from '../../lib/shellTheme.js';
import { recordOpened } from '../../lib/ledger.js';
import { MSG } from '../../lib/messages.js';
import { askEditorTab } from '../editorRelay.js';

export const dropZoneHandlers = {
  // A row drag started in the panel → inject the on-page 4-quadrant drop overlay on that tab,
  // tinted to the current theme accent (resolved from the saved accent key, so the zones match
  // the extension's theme rather than a fixed violet).
  [MSG.DROPZONES_ARM]: (msg) => {
    if (msg.tabId == null) return;
    (async () => {
      let accent = DEFAULT_HL;
      // The Appearance choice rides along UNRESOLVED: 'system' can only be answered by
      // the page the zones land on (lib/shellTheme.js makes the same hand-off).
      let mode = 'system';
      try {
        const l = await chrome.storage.local.get([ACCENT_STORAGE_KEY, THEME_STORAGE_KEY]);
        accent = ACCENT_HEX[l[ACCENT_STORAGE_KEY]] || DEFAULT_HL;
        if (THEME_MODES.includes(l[THEME_STORAGE_KEY])) mode = l[THEME_STORAGE_KEY];
      } catch { /* defaults */ }
      // A live-editor tab gets editor-aware labels (here/incognito/crop act on IT).
      const probe = await askEditorTab(msg.tabId, { type: MSG.EDITOR_STATE, thumbnail: false });
      chrome.scripting.executeScript({ target: { tabId: msg.tabId }, world: 'ISOLATED', func: mountDropZones, args: [accent, !!probe.ok, mode] })
        .catch(() => { /* restricted page — no overlay */ });
    })();
  },
  // The row drag ended without a page drop → tear the overlay down (backstop; it also
  // self-removes on drop / leaving the window / Escape / timeout).
  [MSG.DROPZONES_DISARM]: (msg) => {
    if (msg.tabId == null) return;
    chrome.scripting.executeScript({ target: { tabId: msg.tabId }, world: 'ISOLATED', func: unmountDropZones })
      .catch(() => { /* restricted page — nothing to remove */ });
  },
  // A row was dropped in a quadrant of the on-page overlay → run its action. Reuses the same
  // hand-off machinery as the page API relays (fetch bytes → buildHandoff → open/crop).
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
        // ── Editor-aware: a drop landing ON a live editor acts on THAT editor ──
        // here/incognito import into it (straight in when it's empty; an occupied
        // editor raises the injected replace/new-tab/cancel chooser), and crop
        // imports then opens the editor's OWN crop dialog — never the crop page.
        const probe = tabId != null ? await askEditorTab(tabId, { type: MSG.EDITOR_STATE, thumbnail: false }) : { ok: false };
        if (probe.ok) {
          let mode = 'new';
          // Incognito is never persisted and the saved project stays put, so it
          // needs no replace chooser — it just opens incognito in this editor.
          if (action !== 'incognito' && probe.state?.hasImage) {
            let accent = DEFAULT_HL;
            try { const l = await chrome.storage.local.get(ACCENT_STORAGE_KEY); accent = ACCENT_HEX[l[ACCENT_STORAGE_KEY]] || DEFAULT_HL; } catch { /* default */ }
            const [res] = await chrome.scripting.executeScript({
              target: { tabId }, world: 'ISOLATED', func: mountDropChoice, args: [accent],
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
