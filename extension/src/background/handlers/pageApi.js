// ── Page-API (window.stencil) relays + the two small utility messages ───────
// Fire-and-forget handlers: the page asks, the worker does it, the port closes.
import { fetchAsDataUrl, isImageDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, buildHandoff } from '../../lib/stencil.js';
import { buildStencilSchemeUrl, INLINE_MAX_CHARS } from '../../lib/openIn.js';
import { pruneLedger } from '../../lib/ledger.js';
import { setPinned, siteOf } from '../../lib/pins.js';
import { MSG } from '../../lib/messages.js';

export const pageApiHandlers = {
  // The in-page editor overlay asks us to open a real tab when its iframe is
  // blocked (CSP / mixed content). Doing it here avoids popup blockers.
  [MSG.OPEN_TAB]: (msg) => {
    if (msg.url) chrome.tabs.create({ url: msg.url });
  },
  // The DevTools panel's Settings gear: its context lacks
  // chrome.runtime.openOptionsPage, so the panel asks us to open it.
  [MSG.OPEN_OPTIONS]: () => {
    chrome.runtime.openOptionsPage();
  },
  // The editor-origin bridge reports the editor's live project registry. Prune
  // opened-ledger entries for projects that no longer exist there — scoped to the
  // sender's origin so other editor deployments are left untouched.
  [MSG.REGISTRY]: (msg, sender) => {
    let origin = sender.origin || '';
    if (!origin && sender.url) { try { origin = new URL(sender.url).origin; } catch { origin = ''; } }
    if (origin) pruneLedger(Array.isArray(msg.projects) ? msg.projects : [], origin);
  },
  // ── Page-API (window.stencil) relays ──
  // Open a page image/video in the editor (new tab or in-page modal).
  [MSG.PAGE_OPEN]: (msg, sender) => {
    (async () => {
      try {
        // A caller-supplied dataUrl skips fetchAsDataUrl's allowlist — check it here.
        if (msg.dataUrl && !isImageDataUrl(msg.dataUrl)) throw new Error('dataUrl is not an image');
        // Guard context is sender.tab.url (browser-set) — never msg.resource, which the
        // page could forge to smuggle a private host through the same-host carve-out.
        const dataUrl = msg.dataUrl || await fetchAsDataUrl(msg.url, { pageUrl: sender.tab?.url || '' });
        const { page, desktopScheme } = await getSettings();
        // Desktop hand-off: build the stencil:// scheme URL and let the OS open the app
        // (parity with the context-menu "Open in… Desktop app"), instead of the editor tab.
        if (msg.desktop) {
          if (!desktopScheme) { console.warn('[stencil] open({desktop}) needs a configured desktop scheme'); return; }
          const schemeUrl = buildStencilSchemeUrl({ scheme: desktopScheme, src: dataUrl });
          if (schemeUrl.length > INLINE_MAX_CHARS) { console.warn('[stencil] image too large for an inline desktop hand-off'); return; }
          chrome.tabs.create({ url: schemeUrl });
          return;
        }
        const payload = buildHandoff(
          { name: msg.name || filenameFromUrl(msg.url || 'image'), source: msg.source || msg.url || '' },
          { dataUrl, page, resource: msg.resource || sender.tab?.url || '', incognito: !!msg.incognito }
        );
        if (msg.newTab) await openEditorTab(payload);
        else await launchEditorModal({ ...payload, tabId: sender.tab?.id });
      } catch (err) { console.warn('[stencil] page open failed:', err?.message); }
    })();
  },
  // Pin / unpin a page image/video. The pin is grouped under the page's origin so the
  // options page can list "pins on this site"; the bridge mirrors the write back to the
  // page API (entry.pinned) and any open popup/side panel.
  [MSG.PAGE_PIN]: (msg, sender) => {
    // sender.tab.url first: the recorded resource later serves as same-host guard
    // context (options pin thumbnails), so it must not be page-forgeable.
    const resource = sender.tab?.url || msg.resource || '';
    setPinned({
      source: msg.source || msg.url || '', site: siteOf(resource), resource,
      name: msg.name || filenameFromUrl(msg.url || 'image'), kind: msg.kind || 'image', pinned: !!msg.pin,
    }).catch(() => { /* storage unavailable */ });
  },
  // Open a page image/video in the quick-crop tool.
  [MSG.PAGE_CROP]: (msg, sender) => {
    // Same rule as PAGE_OPEN; a `url` is left to the crop page's own allowlist.
    if (msg.dataUrl && !isImageDataUrl(msg.dataUrl)) return;
    const src = msg.dataUrl || msg.url;
    // sender.tab.url first: the crop page uses this resource as same-host guard context.
    if (src) launchCrop({ src, source: msg.source || msg.url || '', resource: sender.tab?.url || msg.resource || '', tabId: sender.tab?.id });
  },
  // The API's `stencil.enabled = false` — turn the feature off (unregisters the scripts).
  [MSG.PAGE_DISABLE]: () => {
    chrome.storage.sync.set({ exposeWindowStencil: false });
  },
};
