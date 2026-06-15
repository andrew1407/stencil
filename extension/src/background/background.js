// ── Background service worker ───────────────────────────────────────────────
// Owns the right-click context menu. The native menu already offers "Open image"
// / "Save image"; we add Stencil's actions next to them so an image can go
// straight into the editor (as an in-page modal by default). We cover both real
// <img> elements (native 'image' context) and elements with a CSS
// background-image (detected by the content-script probe — see ../lib/contextMenu.js).
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchCrop, getSettings } from '../lib/stencil.js';
import { MENU_ITEMS, resolveContextAction } from '../lib/contextMenu.js';

// Rebuild the menu from scratch. removeAll first so repeated calls (install +
// startup) don't pile up "duplicate id" errors. Called only on install/startup —
// not on every wake — so removeAll never races a right-click.
const buildMenus = () => {
  chrome.contextMenus.removeAll(() => {
    for (const item of MENU_ITEMS) chrome.contextMenus.create(item, () => void chrome.runtime.lastError);
  });
};

// Declared content scripts only inject into pages loaded AFTER the extension is
// installed/updated. Inject the right-click probe into already-open http(s) tabs
// too, so the background context menu works without reloading every tab. The
// probe guards against binding twice (see ctxTarget.js).
const injectProbeIntoOpenTabs = async () => {
  try {
    const tabs = await chrome.tabs.query({ url: ['http://*/*', 'https://*/*'] });
    for (const tab of tabs) {
      if (tab.id == null) continue;
      chrome.scripting.executeScript({
        target: { tabId: tab.id, allFrames: true }, files: ['src/content/ctxTarget.js']
      }).catch(() => { /* restricted page / no access — ignore */ });
    }
  } catch { /* ignore */ }
};

chrome.runtime.onInstalled.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
});
chrome.runtime.onStartup.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
});

// What the probe last resolved under the cursor, per tab. Usually a ready-to-use
// { url } (a background image, or a captured video frame as a data URL). For a
// cross-origin video it's instead the *Promise* of the tab-screenshot crop still
// in flight — the click handler awaits it, so a fast click can't beat the capture
// and end up with nothing. Used because info.srcUrl is absent (backgrounds) or
// points at the wrong thing (for a <video> it's the media file, not a frame).
const lastTargetByTab = new Map();
// Where/what the last right-click pointed at when it was a <video>, per tab:
// { frameId, point:{x,y}, rect, dpr }. Lets the click handler recapture the frame
// in-page at click time even if the recorded frame above is missing (worker
// restart) or stale, and screenshot-crop the right rectangle for tainted media.
const lastVideoByTab = new Map();
chrome.tabs.onRemoved.addListener((tabId) => {
  lastTargetByTab.delete(tabId);
  lastVideoByTab.delete(tabId);
});

chrome.runtime.onMessage.addListener((msg, sender) => {
  // The in-page editor overlay asks us to open a real tab when its iframe is
  // blocked (CSP frame-src / mixed content). Doing it here avoids popup blockers.
  if (msg && msg.type === 'stencil-open-tab' && msg.url) {
    chrome.tabs.create({ url: msg.url });
    return;
  }
  if (msg && msg.type === 'stencil-ctx') {
    const tabId = sender.tab?.id;
    if (tabId == null) return;
    const data = msg.data;
    // Remember the video context (which frame, where, the rect for a screenshot)
    // so the click handler can recapture the current frame in-page.
    lastVideoByTab.set(tabId, data && data.video
      ? { frameId: sender.frameId, point: msg.point || null, rect: data.rect || null, dpr: data.dpr || 1 }
      : null);
    // A tainted (cross-origin) video can't be read via canvas, so grab its frame
    // NOW — at right-click — by cropping a tab screenshot. Doing it here (not at
    // click time) means the editor gets the frame the user actually saw, instead
    // of a later one the still-playing video advanced to.
    if (data && data.video && !data.url) {
      // Store the in-flight promise so a click that arrives before the screenshot
      // finishes awaits it instead of reading null (see lastTargetByTab).
      const pending = captureFrameFromScreenshot(sender.tab && sender.tab.windowId, data.rect, data.dpr)
        .then((url) => (url ? { url, video: true } : null))
        .catch(() => null);
      lastTargetByTab.set(tabId, pending);
    } else {
      lastTargetByTab.set(tabId, data || null);
    }
  }
});

// Encode a Blob as a data URL without FileReader (not available in a SW).
const blobToDataUrl = async (blob) => {
  const bytes = new Uint8Array(await blob.arrayBuffer());
  const CHUNK = 0x8000;
  let binary = '';
  for (let i = 0; i < bytes.length; i += CHUNK) binary += String.fromCharCode.apply(null, bytes.subarray(i, i + CHUNK));
  return `data:${blob.type || 'image/jpeg'};base64,${btoa(binary)}`;
};

// Cap the captured frame's longest side — it rides in the editor launch URL as a
// data URL, and an un-capped retina crop can blow past Chrome's URL limit (the
// editor tab then lands on about:blank). Matches the canvas path in ctxTarget.js.
const FRAME_MAX_SIDE = 1920;

// Crop a tab screenshot to the video's on-screen rectangle. captureVisibleTab
// returns an extension-owned image, so it's never tainted. Downscale to the cap
// and encode JPEG so the data URL stays small enough for the editor launch URL.
const captureFrameFromScreenshot = async (windowId, rect, dpr = 1) => {
  const shot = await chrome.tabs.captureVisibleTab(windowId, { format: 'png' });
  const bitmap = await createImageBitmap(await (await fetch(shot)).blob());
  const sx = Math.max(0, rect.x * dpr), sy = Math.max(0, rect.y * dpr);
  const sw = Math.max(1, Math.round(rect.width * dpr)), sh = Math.max(1, Math.round(rect.height * dpr));
  const s = Math.min(1, FRAME_MAX_SIDE / Math.max(sw, sh));
  const dw = Math.max(1, Math.round(sw * s)), dh = Math.max(1, Math.round(sh * s));
  const canvas = new OffscreenCanvas(dw, dh);
  canvas.getContext('2d').drawImage(bitmap, sx, sy, sw, sh, 0, 0, dw, dh);
  return blobToDataUrl(await canvas.convertToBlob({ type: 'image/jpeg', quality: 0.92 }));
};

// Capture a <video>'s current frame by running canvas readback IN THE PAGE at
// click time — the same approach the popup's scanner uses successfully. This is
// the reliable path: it doesn't depend on a recorded frame surviving an MV3
// worker restart, and it grabs the live frame. Returns a JPEG data URL, or null
// when there's no readable video (cross-origin taint → caller screenshots).
const captureVideoFrameInTab = async (tabId, frameId, point) => {
  if (tabId == null) return null;
  const target = { tabId };
  if (frameId != null) target.frameIds = [frameId];
  try {
    const [res] = await chrome.scripting.executeScript({
      target,
      args: [point ? point.x : null, point ? point.y : null, FRAME_MAX_SIDE],
      func: (px, py, maxSide) => {
        const at = (x, y) => {
          if (x == null || !document.elementsFromPoint) return null;
          for (const el of document.elementsFromPoint(x, y)) {
            if (el.tagName === 'VIDEO') return el;
            const v = el.querySelector && el.querySelector('video');
            if (v) return v;
          }
          if (x != null) {
            for (const v of document.querySelectorAll('video')) {
              const r = v.getBoundingClientRect();
              if (r.width && r.height && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return v;
            }
          }
          return null;
        };
        const largest = () => {
          let best = null, area = -1;
          for (const v of document.querySelectorAll('video')) {
            const a = (v.videoWidth || 0) * (v.videoHeight || 0);
            if (v.readyState >= 2 && a > area) { area = a; best = v; }
          }
          return best;
        };
        const v = at(px, py) || largest();
        if (!v || !v.videoWidth || !v.videoHeight || v.readyState < 2) return null;
        const s = Math.min(1, maxSide / Math.max(v.videoWidth, v.videoHeight));
        const w = Math.max(1, Math.round(v.videoWidth * s)), h = Math.max(1, Math.round(v.videoHeight * s));
        try {
          const c = document.createElement('canvas');
          c.width = w; c.height = h;
          c.getContext('2d').drawImage(v, 0, 0, w, h);
          return c.toDataURL('image/jpeg', 0.92);
        } catch { return null; } // tainted cross-origin media
      }
    });
    return (res && res.result) || null;
  } catch { return null; }
};

// Resolve the image source for a click. A probe-captured video frame (rec.video)
// always wins: for a <video> info.srcUrl is the media file, not a frame — and
// Chrome doesn't always report mediaType:'video' (e.g. a click on a player's
// overlay). Otherwise a real <img>/<svg> uses info.srcUrl; backgrounds use rec.
const resolveSrc = (info, rec) => {
  if (rec && rec.video && rec.url) return rec.url;
  if (info.mediaType === 'video' || info.mediaType === 'audio') return (rec && rec.url) || null;
  if (info.srcUrl) return info.srcUrl;
  return (rec && rec.url) || null;
};

chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  const tabId = tab?.id;
  let rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  // For a cross-origin video rec is the still-running screenshot capture — wait
  // for it rather than resolving to nothing.
  if (rec && typeof rec.then === 'function') rec = await rec;
  let src = resolveSrc(info, rec);

  // Video path. Treat it as a video when Chrome says so, when the probe saw one,
  // or when we already have a probe-captured frame. Unless that frame is already
  // in hand, (re)capture the current frame in-page at click time, then fall back
  // to a screenshot crop. A media URL is NEVER used as the image — better to do
  // nothing than hand the editor a .mp4 it "fails to decode".
  const vinfo = tabId != null ? lastVideoByTab.get(tabId) : null;
  const isVideo = info.mediaType === 'video' || !!vinfo || !!(rec && rec.video);
  if (isVideo && !(rec && rec.video && rec.url)) {
    let frame = await captureVideoFrameInTab(tabId, vinfo ? vinfo.frameId : info.frameId, vinfo && vinfo.point);
    if (!frame && vinfo && vinfo.rect) frame = await captureFrameFromScreenshot(tab.windowId, vinfo.rect, vinfo.dpr);
    src = frame || null;
  }

  // Feed the resolved src as srcUrl: resolveContextAction prefers info.srcUrl, but
  // for a <video> that's the media file, not the frame src picked above — so the
  // raw info.srcUrl must not slip back in here.
  const act = resolveContextAction({ ...info, srcUrl: src }, src);
  if (!act) return;
  try {
    if (act.action === 'crop') {
      await launchCrop({ src: act.src, tabId: tab?.id });   // small in-page modal
      return;
    }
    const { page } = await getSettings();
    const dataUrl = await fetchAsDataUrl(act.src);
    await openEditorTab({ dataUrl, name: filenameFromUrl(act.src), page: { size: page }, incognito: act.incognito });
  } catch (err) {
    console.error('[stencil] context-menu action failed:', err);
  }
});
