// Records what the ctxTarget probe resolved under the cursor and relabels/reveals the menu
// groups that depend on it. Everything here must be synchronous: the native menu is opening.
import { MENU, menuVisibilityFor, DYNAMIC_ITEMS, PREVIEW_ITEMS, pinItemTitle } from '../../lib/contextMenu.js';
import { isPinnedIn, siteOf } from '../../lib/pins.js';
import { MSG } from '../../lib/messages.js';
import { lastTargetByTab, lastVideoByTab, lastPosterByTab, pinsCache } from '../tabState.js';
import { desktopSchemeSet } from '../menus.js';

export const ctxProbeHandlers = {
  [MSG.CTX]: (msg, sender) => {
    const tabId = sender.tab?.id;
    if (tabId == null) return;
    const data = msg.data;
    // Remember the video context so the click handler can recapture in-page.
    lastVideoByTab.set(tabId, data && data.video
      ? { frameId: sender.frameId, point: msg.point || null, rect: data.rect || null, dpr: data.dpr || 1, posterShown: !!data.posterShown }
      : null);
    // A tainted (cross-origin) video has no ready frame here — record nothing and let the
    // click handler capture its CURRENT frame instead.
    lastTargetByTab.set(tabId, (data && data.video && !data.url) ? null : (data || null));
    // The poster (preview image) the probe saw, if any — drives the Preview submenu.
    lastPosterByTab.set(tabId, (data && data.poster) ? data.poster : '');
    // Reveal dynamic background/link items only when the probe found a plain image URL
    // (<img>/<video> use native-context items instead) — never an empty submenu.
    const { bg: showBg, preview: showPreview } = menuVisibilityFor(data);
    for (const id of DYNAMIC_ITEMS)
      chrome.contextMenus.update(id, { visible: showBg }, () => void chrome.runtime.lastError);
    // The background "Open in desktop app" item needs BOTH a background under the cursor AND a
    // configured scheme (unlike the rest of the bg group, which only needs the background).
    chrome.contextMenus.update(MENU.bgDesktop, { visible: showBg && desktopSchemeSet }, () => void chrome.runtime.lastError);
    // Reveal the video Preview submenu only when the probed <video> has a poster —
    // otherwise its actions would be silent no-ops.
    for (const id of PREVIEW_ITEMS)
      chrome.contextMenus.update(id, { visible: showPreview }, () => void chrome.runtime.lastError);
    // Relabel the pin item SYNCHRONOUSLY off the in-memory pins cache — an awaited storage
    // read loses the race against the native menu appearing.
    const site = siteOf(sender.tab?.url || '');
    const relabel = (id, source, kind) => {
      if (!source) return;
      chrome.contextMenus.update(id, { title: pinItemTitle(isPinnedIn(pinsCache, site, source), kind) },
        () => void chrome.runtime.lastError);
    };
    relabel(MENU.pin, data && data.imgUrl, 'image');
    relabel(MENU.bgPin, data && !data.video && data.url, 'image');
    relabel(MENU.framePin, data && data.video && (data.videoUrl || data.poster), 'video');
  },
};
