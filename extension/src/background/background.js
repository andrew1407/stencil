// ── Background service worker ───────────────────────────────────────────────
// Owns the right-click context menu: adds Stencil's actions so an image can go
// straight into the editor. Covers real <img> (native 'image' context) and CSS
// background-image elements (detected by the content-script probe, ctxTarget.js).
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchCrop, getSettings } from '../lib/stencil.js';
import { MENU_ITEMS, resolveContextAction } from '../lib/contextMenu.js';

// Rebuild the menu from scratch. removeAll first so install+startup don't pile up
// "duplicate id" errors. Only on install/startup, so it never races a right-click.
const buildMenus = () => {
  chrome.contextMenus.removeAll(() => {
    for (const item of MENU_ITEMS) chrome.contextMenus.create(item, () => void chrome.runtime.lastError);
  });
};

// Declared content scripts only inject into pages loaded AFTER install/update.
// Inject the probe into already-open http(s) tabs too, so the menu works without
// reloading every tab. The probe guards against binding twice (see ctxTarget.js).
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

// What the probe last resolved under the cursor, per tab. Usually a ready { url }
// (background image, or a captured video frame). For a cross-origin video it's the
// *Promise* of the in-flight screenshot crop — the click handler awaits it so a
// fast click can't beat the capture. Needed because info.srcUrl is absent
// (backgrounds) or wrong (a <video>'s media file, not a frame).
const lastTargetByTab = new Map();
// Where the last right-click pointed when it was a <video>, per tab:
// { frameId, point, rect, dpr }. Lets the click handler recapture the frame in-page
// at click time (if the recorded one is missing/stale) and screenshot-crop the
// right rectangle for tainted media.
const lastVideoByTab = new Map();
chrome.tabs.onRemoved.addListener((tabId) => {
  lastTargetByTab.delete(tabId);
  lastVideoByTab.delete(tabId);
});

chrome.runtime.onMessage.addListener((msg, sender) => {
  // The in-page editor overlay asks us to open a real tab when its iframe is
  // blocked (CSP / mixed content). Doing it here avoids popup blockers.
  if (msg && msg.type === 'stencil-open-tab' && msg.url) {
    chrome.tabs.create({ url: msg.url });
    return;
  }
  if (msg && msg.type === 'stencil-ctx') {
    const tabId = sender.tab?.id;
    if (tabId == null) return;
    const data = msg.data;
    // Remember the video context so the click handler can recapture in-page.
    lastVideoByTab.set(tabId, data && data.video
      ? { frameId: sender.frameId, point: msg.point || null, rect: data.rect || null, dpr: data.dpr || 1 }
      : null);
    // A tainted (cross-origin) video can't be read via canvas, so grab its frame
    // NOW — at right-click — by cropping a tab screenshot, so the editor gets the
    // frame the user saw, not a later one the still-playing video advanced to.
    if (data && data.video && !data.url) {
      // Store the in-flight promise so a click arriving before it finishes awaits
      // it instead of reading null (see lastTargetByTab).
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

// Cap the captured frame's longest side: it rides in the editor launch URL as a
// data URL, and an un-capped retina crop overflows Chrome's URL limit (about:blank).
const FRAME_MAX_SIDE = 1920;

// Crop a tab screenshot to the video's on-screen rectangle. captureVisibleTab
// returns an extension-owned (never tainted) image; downscale + JPEG-encode so the
// data URL stays small enough for the launch URL.
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
// click time (like the popup scanner). Reliable — doesn't depend on a recorded
// frame surviving a worker restart, and grabs the live frame. Returns a JPEG data
// URL, or null when no readable video (cross-origin taint → caller screenshots).
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
// wins: for a <video> info.srcUrl is the media file, not a frame, and Chrome
// doesn't always report mediaType:'video'. Otherwise <img>/<svg> use info.srcUrl,
// backgrounds use rec.
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

  // Video path: treat as video when Chrome says so, the probe saw one, or we have a
  // probe-captured frame. Unless that frame is in hand, (re)capture in-page at click
  // time, then fall back to a screenshot crop. A media URL is NEVER used as the
  // image — better to do nothing than hand the editor a .mp4 it can't decode.
  const vinfo = tabId != null ? lastVideoByTab.get(tabId) : null;
  const isVideo = info.mediaType === 'video' || !!vinfo || !!(rec && rec.video);
  if (isVideo && !(rec && rec.video && rec.url)) {
    let frame = await captureVideoFrameInTab(tabId, vinfo ? vinfo.frameId : info.frameId, vinfo && vinfo.point);
    if (!frame && vinfo && vinfo.rect) frame = await captureFrameFromScreenshot(tab.windowId, vinfo.rect, vinfo.dpr);
    src = frame || null;
  }

  // Feed the resolved src as srcUrl so the raw info.srcUrl (a <video>'s media file)
  // can't slip back in over the frame src picked above.
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
