// ── Context menu: build + desktop-item visibility ───────────────────────────
// The menu is rebuilt from scratch on every worker start; the desktop-app items are
// revealed only when a URL scheme is configured.
import { getSettings } from '../lib/stencil.js';
import { MENU_ITEMS, STATIC_DESKTOP_ITEMS } from '../lib/contextMenu.js';

// Rebuild the menu from scratch; removeAll first avoids "duplicate id" on repeated
// builds. onInstalled/onStartup aren't reliable per reload, so this also runs at
// top level on each worker start (below).
export const buildMenus = () => {
  chrome.contextMenus.removeAll(() => {
    for (const item of MENU_ITEMS) chrome.contextMenus.create(item, () => void chrome.runtime.lastError);
    console.info(`[stencil] context menu built: ${MENU_ITEMS.length} items`);
    // Reveal the desktop-app items only if a scheme is configured (they're created hidden).
    syncDesktopMenuVisibility();
  });
};

// Whether a desktop URL scheme is configured — gates the "Open in desktop app" menu items
// (they no-op without a scheme, so hide them). Cached for the synchronous CTX probe handler.
export let desktopSchemeSet = true;

// Show/hide the desktop-app hand-off items to match the configured scheme. The STATIC items
// (image / video-frame) toggle on the scheme alone; MENU.bgDesktop is revealed by the probe
// (CTX handler) gated on this flag, so it isn't touched here.
export const syncDesktopMenuVisibility = async () => {
  try { const { desktopScheme } = await getSettings(); desktopSchemeSet = !!desktopScheme; }
  catch { desktopSchemeSet = true; }
  for (const id of STATIC_DESKTOP_ITEMS)
    chrome.contextMenus.update(id, { visible: desktopSchemeSet }, () => void chrome.runtime.lastError);
};
