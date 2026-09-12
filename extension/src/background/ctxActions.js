// One async handler per click group: toolbar action items, the video-preview submenu, pin
// items, and the default image/video-frame path.
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, buildHandoff } from '../lib/stencil.js';
import { MENU, resolveContextAction, PIN_ITEMS } from '../lib/contextMenu.js';
import { buildStencilSchemeUrl, INLINE_MAX_CHARS } from '../lib/openIn.js';
import { setPinned, loadPins, isPinnedIn, siteOf } from '../lib/pins.js';
import { lastTargetByTab, lastVideoByTab, lastPosterByTab } from './tabState.js';
import { captureFrameFromScreenshot, captureVideoFrameInTab, captureVideoFrameViaFetch } from './frameCapture.js';

// A probe-captured video frame (rec.video) wins: for a <video>, info.srcUrl is the media file.
const resolveSrc = (info, rec) => {
  if (rec && rec.video && rec.url) return rec.url;
  if (info.mediaType === 'video' || info.mediaType === 'audio') return (rec && rec.url) || null;
  if (info.srcUrl) return info.srcUrl;
  return (rec && rec.url) || null;
};

// The incognito variant opens in an incognito window, so its project storage is throwaway.
const openFreshEditor = async (info) => {
  try {
    const { editorUrl } = await getSettings();
    if (info.menuItemId === MENU.actionOpenIncognito) await chrome.windows.create({ url: editorUrl, incognito: true });
    else await chrome.tabs.create({ url: editorUrl });
  } catch (err) {
    console.error('[stencil] open-editor action failed:', err);
  }
};

// Preview submenu: act on the video's POSTER (a normal image URL), never a frame.
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

// A pin keys on the openable SOURCE URL, the same thing "open in new tab" uses.
const togglePinFromMenu = async (info, tab, tabId) => {
  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  const poster = tabId != null ? lastPosterByTab.get(tabId) : '';
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

// Oversized inline payloads are refused: the OS stencil:// launch can't carry them.
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

// Default: an <img> / background / <video>-frame click resolves the image bytes.
const openImageOrFrame = async (info, tab, tabId) => {
  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  let src = resolveSrc(info, rec);

  // A media URL is NEVER used as the image (.mp4 won't decode); sourceUrl is captured
  // before `src` is overwritten with a captured frame.
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
      src = poster;   // not played → the poster beats a black frame 0
    } else if (vinfo && vinfo.rect) {
      src = await captureFrameFromScreenshot(tab.windowId, vinfo.rect, vinfo.dpr);
    } else {
      src = poster || null;  // last resort: any poster we have
    }
  }

  // Feed the resolved src as srcUrl so the raw info.srcUrl (a video's media file) can't
  // slip back in over the frame src picked above.
  const act = resolveContextAction({ ...info, srcUrl: src }, src);
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
    const payload = buildHandoff({ name: filenameFromUrl(act.src), source: sourceUrl }, { dataUrl, page, resource, incognito: act.incognito, open: act.open });
    if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId: tab?.id });   // in-page editor modal
    else await openEditorTab(payload);
  } catch (err) {
    console.error('[stencil] context-menu action failed:', err);
  }
};

export const resolveClickHandler = (info) => {
  if (info.menuItemId === MENU.actionOpen || info.menuItemId === MENU.actionOpenIncognito) return openFreshEditor;
  if (typeof info.menuItemId === 'string' && info.menuItemId.startsWith('stencil-preview-')) return actOnPreview;
  if (PIN_ITEMS.includes(info.menuItemId)) return togglePinFromMenu;
  return openImageOrFrame;
};
