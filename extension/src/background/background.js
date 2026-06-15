// ── Background service worker ───────────────────────────────────────────────
// Owns the image right-click context menu. The native menu already offers
// "Open image" / "Save image"; we add Stencil's actions next to them so an
// image can go straight into the editor (as an in-page modal by default).
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchCrop, getSettings } from '../lib/stencil.js';

const MENU = { parent: 'stencil-parent', open: 'stencil-open', openIncognito: 'stencil-open-incognito', crop: 'stencil-crop' };

// The contextMenus API has no per-item image icon, so we (a) ship PNG action
// icons — Chrome then shows the Stencil icon next to the "Stencil" group — and
// (b) prefix each item with a glyph matching the popup's action icons.
function buildMenus() {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({ id: MENU.parent, title: 'Stencil', contexts: ['image'] });
    chrome.contextMenus.create({ id: MENU.open, parentId: MENU.parent, title: '✎ Open image in Stencil editor', contexts: ['image'] });
    chrome.contextMenus.create({ id: MENU.openIncognito, parentId: MENU.parent, title: '🕶 Open in Stencil (incognito)', contexts: ['image'] });
    chrome.contextMenus.create({ id: MENU.crop, parentId: MENU.parent, title: '✂ Crop image in Stencil…', contexts: ['image'] });
  });
}

chrome.runtime.onInstalled.addListener(buildMenus);
chrome.runtime.onStartup.addListener(buildMenus);

// The in-page editor overlay asks us to open a real tab when its iframe is
// blocked (CSP frame-src / mixed content). Doing it here avoids popup blockers.
chrome.runtime.onMessage.addListener((msg) => {
  if (msg && msg.type === 'stencil-open-tab' && msg.url) chrome.tabs.create({ url: msg.url });
});

chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  const src = info.srcUrl;
  if (!src) return;
  try {
    if (info.menuItemId === MENU.crop) {
      await launchCrop({ src, tabId: tab?.id });   // small in-page modal
      return;
    }
    const incognito = info.menuItemId === MENU.openIncognito;
    if (info.menuItemId === MENU.open || incognito) {
      const { page } = await getSettings();
      const dataUrl = await fetchAsDataUrl(src);
      await openEditorTab({ dataUrl, name: filenameFromUrl(src), page: { size: page }, incognito });
    }
  } catch (err) {
    console.error('[stencil] context-menu action failed:', err);
  }
});
