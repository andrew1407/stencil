// ── Context-menu click handlers ─────────────────────────────────────────────
// One async handler per click group: the toolbar action items, the video-preview
// submenu, the pin items, and the default image / video-frame path.
// `resolveClickHandler` routes an incoming click to exactly one of them, in order.
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, buildHandoff } from '../lib/stencil.js';
import { MENU, resolveContextAction, PIN_ITEMS } from '../lib/contextMenu.js';
import { buildStencilSchemeUrl, INLINE_MAX_CHARS } from '../lib/openIn.js';
import { setPinned, loadPins, isPinnedIn, siteOf } from '../lib/pins.js';
import { lastTargetByTab, lastVideoByTab, lastPosterByTab } from './tabState.js';
import { captureFrameFromScreenshot, captureVideoFrameInTab, captureVideoFrameViaFetch } from './frameCapture.js';

// Resolve the image source for a click. A probe-captured video frame (rec.video) wins:
// for a <video> info.srcUrl is the media file (not a frame) and Chrome doesn't always
// report mediaType:'video'. Otherwise <img>/<svg> use info.srcUrl, backgrounds use rec.
const resolveSrc = (info, rec) => {
  if (rec && rec.video && rec.url) return rec.url;
  if (info.mediaType === 'video' || info.mediaType === 'audio') return (rec && rec.url) || null;
  if (info.srcUrl) return info.srcUrl;
  return (rec && rec.url) || null;
};

// ── Toolbar-icon menu: open a fresh Stencil editor (no image). The incognito variant
// opens it in an incognito window, so the editor's own project storage is throwaway. ──
const openFreshEditor = async (info) => {
  try {
    const { editorUrl } = await getSettings();
    if (info.menuItemId === MENU.actionOpenIncognito) await chrome.windows.create({ url: editorUrl, incognito: true });
    else await chrome.tabs.create({ url: editorUrl });
  } catch (err) {
    console.error('[stencil] open-editor action failed:', err);
  }
};

// ── Preview submenu: act on the video's POSTER (a normal image URL), never a
// frame. A no-op when the right-clicked element had no poster. ──
const actOnPreview = async (info, tab, tabId) => {
  const poster = (tabId != null ? lastPosterByTab.get(tabId) : '') || '';
  const act = resolveContextAction({ menuItemId: info.menuItemId, srcUrl: poster }, poster);
  if (!act) return;   // no poster on this element
  const resource = tab?.url || '';
  try {
    if (act.action === 'open-tab') {
      await chrome.tabs.create({ url: act.src });
      return;
    }
    if (act.action === 'crop') {
      await launchCrop({ src: act.src, source: act.src, resource, tabId });
      return;
    }
    const { page } = await getSettings();
    const dataUrl = await fetchAsDataUrl(act.src, { pageUrl: resource });
    const payload = buildHandoff({ name: filenameFromUrl(act.src), source: act.src }, { dataUrl, page, resource, incognito: act.incognito });
    if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId });
    else await openEditorTab(payload);
  } catch (err) {
    console.error('[stencil] preview action failed:', err);
  }
};

// ── Pin / unpin the right-clicked image or video on this site. No editor launch and
// no frame capture — a pin keys on the openable SOURCE URL (a video's media URL, an
// image/background's src), the same thing "open in new tab" uses. Toggles. ──
const togglePinFromMenu = async (info, tab, tabId) => {
  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  const poster = tabId != null ? lastPosterByTab.get(tabId) : '';
  // The openable source + kind depend on which menu item fired: a background/overlay uses
  // the probe's recorded URL; a <video> frame uses the media URL (info.srcUrl) or poster
  // fallback; a plain image uses info.srcUrl.
  const byItem = {
    [MENU.bgPin]: { source: (rec && rec.url) || info.srcUrl || '', kind: 'background' },
    [MENU.framePin]: { source: info.srcUrl || poster || '', kind: 'video' },
  };
  const { source, kind } = byItem[info.menuItemId]
    || { source: info.srcUrl || (rec && rec.url) || '', kind: 'image' };
  if (!source) return;
  const resource = tab?.url || '';
  const site = siteOf(resource);
  try {
    const pinned = isPinnedIn(await loadPins(), site, source);
    await setPinned({ source, site, resource, name: filenameFromUrl(source), kind, pinned: !pinned });
  } catch (err) {
    console.error('[stencil] pin action failed:', err);
  }
};

// ── "Open in desktop app" (context menu) → hand the image over via the stencil:// URL
// scheme with the bytes inline. chrome.tabs.create fires the OS external-protocol prompt
// from the SW; oversized inline payloads are refused (the OS launch can't carry them). ──
const openInDesktopFromMenu = async (src, pageUrl = '') => {
  const { desktopScheme } = await getSettings();
  if (!desktopScheme || !src) return;
  try {
    const dataUrl = await fetchAsDataUrl(src, { pageUrl });
    const url = buildStencilSchemeUrl({ scheme: desktopScheme, src: dataUrl });
    if (url.length > INLINE_MAX_CHARS) { console.warn('[stencil] image too large for an inline desktop hand-off'); return; }
    chrome.tabs.create({ url });
  } catch (err) {
    console.error('[stencil] open-in-desktop failed:', err);
  }
};

// ── Default: an <img> / background / <video>-frame click → resolve the image bytes
// (capturing a video frame as needed) and open / crop them in the editor. ──
const openImageOrFrame = async (info, tab, tabId) => {
  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  let src = resolveSrc(info, rec);

  // Video path: capture in-page at click time → re-fetch bytes → screenshot-crop. A media
  // URL is NEVER used as the image (.mp4 won't decode); sourceUrl is captured for
  // provenance before `src` is overwritten below with a frame data URL.
  const sourceUrl = src || '';
  const resource = tab?.url || '';

  const vinfo = tabId != null ? lastVideoByTab.get(tabId) : null;
  const poster = (tabId != null ? lastPosterByTab.get(tabId) : '') || '';
  const isVideo = info.mediaType === 'video' || !!vinfo || !!(rec && rec.video);
  if (isVideo && !(rec && rec.video && rec.url)) {
    const frameId = vinfo ? vinfo.frameId : info.frameId;
    const probe = await captureVideoFrameInTab(tabId, frameId, vinfo && vinfo.point);
    let frame = probe && probe.frame ? probe.frame : null;
    if (!frame && probe && probe.src) frame = await captureVideoFrameViaFetch(tabId, frameId, probe.src, probe.t, resource);
    if (frame) {
      src = frame;
    } else if (poster && (!vinfo || vinfo.posterShown)) {
      // No real frame AND the video is on its poster (not played) → use the poster.
      // The poster URL is cleaner than a screenshot crop, and avoids a black frame 0.
      src = poster;
    } else if (vinfo && vinfo.rect) {
      // Playing but cross-origin / unreadable → screenshot-crop the on-screen frame.
      src = await captureFrameFromScreenshot(tab.windowId, vinfo.rect, vinfo.dpr);
    } else {
      src = poster || null;  // last resort: any poster we have
    }
  }

  // Feed the resolved src as srcUrl so the raw info.srcUrl (a <video>'s media file)
  // can't slip back in over the frame src picked above.
  const act = resolveContextAction({ ...info, srcUrl: src }, src);
  // Nothing resolvable under the cursor. The menu entry shouldn't have been reachable at
  // all (the dynamic group is revealed only on a probe hit, and the static group needs a
  // native image/video context), so this is the "the page changed under us" case.
  if (!act) {
    console.warn('[stencil] context-menu click found nothing to act on (the target moved or the frame could not be read)');
    return;
  }
  try {
    if (act.action === 'crop') {
      await launchCrop({ src: act.src, source: sourceUrl, resource, tabId: tab?.id });   // small in-page modal
      return;
    }
    if (act.action === 'desktop') { await openInDesktopFromMenu(act.src, resource); return; }
    const { page } = await getSettings();
    const dataUrl = await fetchAsDataUrl(act.src, { pageUrl: resource });
    // `act.open` ('resume') only set by the Resume item; undefined drops out of the
    // JSON payload so a plain open imports fresh, as before.
    const payload = buildHandoff({ name: filenameFromUrl(act.src), source: sourceUrl }, { dataUrl, page, resource, incognito: act.incognito, open: act.open });
    if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId: tab?.id });   // in-page editor modal
    else await openEditorTab(payload);
  } catch (err) {
    console.error('[stencil] context-menu action failed:', err);
  }
};

// Route a click to its handler: the two toolbar action items, then the preview-*
// submenu, then the pin items, then the default image / video-frame path.
export const resolveClickHandler = (info) => {
  if (info.menuItemId === MENU.actionOpen || info.menuItemId === MENU.actionOpenIncognito) return openFreshEditor;
  if (typeof info.menuItemId === 'string' && info.menuItemId.startsWith('stencil-preview-')) return actOnPreview;
  if (PIN_ITEMS.includes(info.menuItemId)) return togglePinFromMenu;
  return openImageOrFrame;
};
