// ── Background service worker ───────────────────────────────────────────────
// Owns the right-click context menu: adds Stencil's actions so an image can go
// straight into the editor. Covers real <img> (native 'image' context) and CSS
// background-image elements (detected by the content-script probe, ctxTarget.js).
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings } from '../lib/stencil.js';
import { MENU_ITEMS, resolveContextAction, DYNAMIC_ITEMS, PREVIEW_ITEMS } from '../lib/contextMenu.js';
import { pruneLedger } from '../lib/ledger.js';
import { MSG } from '../lib/messages.js';

// Rebuild the menu from scratch. removeAll first so repeated builds don't pile up
// "duplicate id" errors. onInstalled/onStartup don't reliably fire on every reload,
// so this also runs at top level on each worker start (below).
const buildMenus = () => {
  chrome.contextMenus.removeAll(() => {
    for (const item of MENU_ITEMS) chrome.contextMenus.create(item, () => void chrome.runtime.lastError);
    console.info(`[stencil] context menu built: ${MENU_ITEMS.length} items`);
  });
};

// Build immediately on worker startup (covers reloads where onInstalled/onStartup
// don't fire). Idempotent: removeAll precedes every create.
buildMenus();

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
  } catch {
    /* ignore */
  }
};

// ── Editor bridge ───────────────────────────────────────────────────────────
// The editor app is cross-origin: its project registry lives in its own localStorage,
// unreadable from here. A content script injected ONLY into the configured editor
// origin reads that registry (same-origin) and reports it back, so we can prune
// opened-ledger entries for projects the user deleted. Registration follows the
// editorUrl setting and is refreshed when it changes; already-open editor tabs are
// injected on startup so the bridge works without a reload.
const BRIDGE_ID = 'stencil-editor-bridge';
const BRIDGE_FILE = 'src/content/editorBridge.js';

// `${origin}/*` match pattern for the configured editor, or null when the editorUrl
// isn't an http(s) origin we can scope a content script to.
const editorOriginPattern = async () => {
  try {
    const { editorUrl } = await getSettings();
    const origin = new URL(editorUrl).origin;
    return origin.startsWith('http') ? `${origin}/*` : null;
  } catch {
    return null;
  }
};

const registerEditorBridge = async () => {
  // Clear any prior registration first so an editorUrl change doesn't leave the old
  // origin registered (and a re-register doesn't throw "duplicate id").
  try { await chrome.scripting.unregisterContentScripts({ ids: [BRIDGE_ID] }); } catch { /* not registered */ }
  const pattern = await editorOriginPattern();
  if (!pattern) return;
  try {
    await chrome.scripting.registerContentScripts([{
      id: BRIDGE_ID, js: [BRIDGE_FILE], matches: [pattern], runAt: 'document_start', allFrames: false
    }]);
  } catch (e) {
    console.warn('[stencil] could not register editor bridge:', e?.message);
  }
};

// Declared/registered scripts only inject on future loads; cover editor tabs open now.
const injectBridgeIntoOpenEditors = async () => {
  const pattern = await editorOriginPattern();
  if (!pattern) return;
  try {
    const tabs = await chrome.tabs.query({ url: [pattern] });
    for (const tab of tabs) {
      if (tab.id == null) continue;
      chrome.scripting.executeScript({ target: { tabId: tab.id }, files: [BRIDGE_FILE] })
        .catch(() => { /* restricted page / no access — ignore */ });
    }
  } catch {
    /* ignore */
  }
};

const setUpEditorBridge = () => { registerEditorBridge(); injectBridgeIntoOpenEditors(); };

// ── Page scripting API (opt-in window.stencil) ──────────────────────────────
// When the user enables it, inject two scripts into every page: a MAIN-world script
// that defines window.stencil (so its entries hold live DOM elements), and an
// ISOLATED bridge that relays the API's action requests to this worker. Off by
// default; registered/unregistered as the setting flips.
const PAGE_API = [
  { id: 'stencil-page-bridge', file: 'src/content/pageApiBridge.js', world: 'ISOLATED', runAt: 'document_start' },
  { id: 'stencil-page-main', file: 'src/content/pageApiMain.js', world: 'MAIN', runAt: 'document_idle' },
];

const registerPageApi = async () => {
  try { await chrome.scripting.unregisterContentScripts({ ids: PAGE_API.map((s) => s.id) }); } catch { /* not registered */ }
  const { exposeWindowStencil } = await getSettings();
  if (!exposeWindowStencil) return;
  try {
    await chrome.scripting.registerContentScripts(PAGE_API.map((s) => ({
      id: s.id, js: [s.file], matches: ['<all_urls>'], runAt: s.runAt, allFrames: false, world: s.world,
    })));
  } catch (e) {
    console.warn('[stencil] could not register page API:', e?.message);
  }
};

// Cover already-open http(s) tabs so enabling the API works without a reload.
const injectPageApiIntoOpenTabs = async () => {
  const { exposeWindowStencil } = await getSettings();
  if (!exposeWindowStencil) return;
  try {
    const tabs = await chrome.tabs.query({ url: ['http://*/*', 'https://*/*'] });
    for (const tab of tabs) {
      if (tab.id == null) continue;
      for (const s of PAGE_API)
        chrome.scripting.executeScript({ target: { tabId: tab.id }, world: s.world, files: [s.file] })
          .catch(() => { /* restricted page — ignore */ });
    }
  } catch { /* ignore */ }
};

const setUpPageApi = () => { registerPageApi(); injectPageApiIntoOpenTabs(); };

// React to settings changes: re-scope the editor bridge (editorUrl) and toggle the
// page API (exposeWindowStencil).
chrome.storage.onChanged.addListener((changes, area) => {
  if (area !== 'sync') return;
  if (changes.editorUrl) setUpEditorBridge();
  if (changes.exposeWindowStencil) setUpPageApi();
});

chrome.runtime.onInstalled.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
  setUpEditorBridge();
  setUpPageApi();
});
chrome.runtime.onStartup.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
  setUpEditorBridge();
  setUpPageApi();
});

// Also set up on every worker start (onInstalled/onStartup don't fire on every wake).
setUpEditorBridge();
registerPageApi();   // re-asserts registration (injection into open tabs only on explicit toggle/startup)

// What the probe last resolved under the cursor, per tab (a ready { url } for a
// background or captured frame). Needed because info.srcUrl is absent (backgrounds)
// or wrong (a <video>'s media file, not a frame).
const lastTargetByTab = new Map();
// Where the last right-click pointed when it was a <video>, per tab: { frameId,
// point, rect, dpr }. Lets the click handler recapture in-page and screenshot-crop
// the right rect for tainted media.
const lastVideoByTab = new Map();
// Poster URL of the last right-clicked <video>, per tab — drives the Preview submenu.
const lastPosterByTab = new Map();
chrome.tabs.onRemoved.addListener((tabId) => {
  lastTargetByTab.delete(tabId);
  lastVideoByTab.delete(tabId);
  lastPosterByTab.delete(tabId);
});

chrome.runtime.onMessage.addListener((msg, sender) => {
  // The in-page editor overlay asks us to open a real tab when its iframe is
  // blocked (CSP / mixed content). Doing it here avoids popup blockers.
  if (msg && msg.type === MSG.OPEN_TAB && msg.url) {
    chrome.tabs.create({ url: msg.url });
    return;
  }
  // The editor-origin bridge reports the editor's live project registry. Prune
  // opened-ledger entries for projects that no longer exist there — scoped to the
  // sender's origin so other editor deployments are left untouched.
  if (msg && msg.type === MSG.REGISTRY) {
    let origin = sender.origin || '';
    if (!origin && sender.url) { try { origin = new URL(sender.url).origin; } catch { origin = ''; } }
    if (origin) pruneLedger(Array.isArray(msg.projects) ? msg.projects : [], origin);
    return;
  }
  // ── Page-API (window.stencil) relays ──
  // Open a page image/video in the editor (new tab or in-page modal).
  if (msg && msg.type === MSG.PAGE_OPEN) {
    (async () => {
      try {
        const dataUrl = msg.dataUrl || await fetchAsDataUrl(msg.url);
        const { page } = await getSettings();
        const payload = {
          dataUrl, name: msg.name || filenameFromUrl(msg.url || 'image'), page: { size: page },
          source: msg.source || msg.url || '', resource: msg.resource || sender.tab?.url || '', incognito: !!msg.incognito,
        };
        if (msg.newTab) await openEditorTab(payload);
        else await launchEditorModal({ ...payload, tabId: sender.tab?.id });
      } catch (err) { console.warn('[stencil] page open failed:', err?.message); }
    })();
    return;
  }
  // Open a page image/video in the quick-crop tool.
  if (msg && msg.type === MSG.PAGE_CROP) {
    const src = msg.dataUrl || msg.url;
    if (src) launchCrop({ src, source: msg.source || msg.url || '', resource: msg.resource || sender.tab?.url || '', tabId: sender.tab?.id });
    return;
  }
  // The API's `stencil.enabled = false` — turn the feature off (unregisters the scripts).
  if (msg && msg.type === MSG.PAGE_DISABLE) {
    chrome.storage.sync.set({ exposeWindowStencil: false });
    return;
  }
  if (msg && msg.type === MSG.CTX) {
    const tabId = sender.tab?.id;
    if (tabId == null) return;
    const data = msg.data;
    // Remember the video context so the click handler can recapture in-page.
    lastVideoByTab.set(tabId, data && data.video
      ? { frameId: sender.frameId, point: msg.point || null, rect: data.rect || null, dpr: data.dpr || 1, posterShown: !!data.posterShown }
      : null);
    // A tainted (cross-origin) video has no ready frame here — its CURRENT frame is
    // captured at click time (in-page CORS readback → extension byte re-fetch →
    // screenshot crop), so record nothing and let the click handler do the work.
    lastTargetByTab.set(tabId, (data && data.video && !data.url) ? null : (data || null));
    // The poster (preview image) the probe saw, if any — drives the Preview submenu.
    lastPosterByTab.set(tabId, (data && data.poster) ? data.poster : '');
    // Reveal the dynamic background/link items only when the probe found a plain image
    // URL (not a <video>); hide otherwise so they never show on a plain element.
    // <img>/<video> use native-context items, untouched by this toggle.
    const showBg = !!(data && data.url && !data.video);
    for (const id of DYNAMIC_ITEMS)
      chrome.contextMenus.update(id, { visible: showBg }, () => void chrome.runtime.lastError);
    // Reveal the video Preview submenu only when the probed <video> has a poster, so a
    // posterless video no longer shows a submenu whose actions would be a silent no-op.
    const showPreview = !!(data && data.video && data.poster);
    for (const id of PREVIEW_ITEMS)
      chrome.contextMenus.update(id, { visible: showPreview }, () => void chrome.runtime.lastError);
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

// Capture a <video>'s current frame by in-page canvas readback at click time. Tries
// a direct draw (same-origin), then a fresh crossOrigin="anonymous" video at the same
// src/time (works when the CDN serves CORS though the page's <video> is tainted).
// Returns { frame } (JPEG data URL), { src, t } (tainted; caller re-fetches), or null.
const captureVideoFrameInTab = async (tabId, frameId, point) => {
  if (tabId == null) return null;
  const target = { tabId };
  if (frameId != null) target.frameIds = [frameId];
  try {
    const [res] = await chrome.scripting.executeScript({
      target,
      args: [point ? point.x : null, point ? point.y : null, FRAME_MAX_SIDE],
      func: async (px, py, maxSide) => {
        // Smallest <video> whose box contains the cursor — spatially correct even
        // under an overlay, and never jumps to another video the way
        // querySelector('video') on an ancestor would.
        const at = (x, y) => {
          if (x == null) return null;
          let best = null, bestArea = Infinity;
          for (const v of document.querySelectorAll('video')) {
            const r = v.getBoundingClientRect();
            if (r.width && r.height && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
              const a = r.width * r.height;
              if (a < bestArea) { bestArea = a; best = v; }
            }
          }
          if (best) return best;
          if (document.elementsFromPoint) for (const el of document.elementsFromPoint(x, y)) if (el.tagName === 'VIDEO') return el;
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
        const draw = (video) => {
          const s = Math.min(1, maxSide / Math.max(video.videoWidth, video.videoHeight));
          const w = Math.max(1, Math.round(video.videoWidth * s)), h = Math.max(1, Math.round(video.videoHeight * s));
          const c = document.createElement('canvas');
          c.width = w; c.height = h;
          c.getContext('2d').drawImage(video, 0, 0, w, h);
          return c.toDataURL('image/jpeg', 0.92);
        };
        // With a cursor point use only the video under it; largest() is the no-point
        // fallback (Chrome video context without a probe point).
        const v = px != null ? at(px, py) : largest();
        if (!v || !v.videoWidth || !v.videoHeight || v.readyState < 2) return null;
        // Paused at the very start → the poster is showing, not a real frame; let
        // the caller fall back to the poster instead of grabbing a black frame 0.
        if (v.paused && !v.currentTime) return null;
        try {
          return { frame: draw(v) };
        } catch {
          /* tainted — try CORS below */
        }
        const src = v.currentSrc || v.src || '';
        const t = v.currentTime || 0;
        if (!src) return null;
        const frame = await new Promise((resolve) => {
          const nv = document.createElement('video');
          nv.crossOrigin = 'anonymous'; nv.muted = true; nv.preload = 'auto'; nv.src = src;
          let done = false;
          const fin = (x) => { if (!done) { done = true; resolve(x); } };
          nv.addEventListener('loadeddata', () => {
            try {
              nv.currentTime = Math.min(t, Math.max(0, (nv.duration || t) - 0.01));
            } catch {
              fin(null);
            }
          });
          nv.addEventListener('seeked', () => {
            try {
              fin(draw(nv));
            } catch {
              fin(null);
            }
          });
          nv.addEventListener('error', () => fin(null));
          setTimeout(() => fin(null), 8000);
        });
        return frame ? { frame } : { src, t };
      }
    });
    return (res && res.result) || null;
  } catch {
    return null;
  }
};

// Current-frame capture for a tainted, non-CORS video: fetch the bytes with the
// extension's host permissions (bypasses page CORS), ship them into the page as a
// blob URL, and draw the frame at the recorded time. Skips huge media (caller falls
// back to a screenshot crop). Returns a JPEG data URL or null.
const captureVideoFrameViaFetch = async (tabId, frameId, src, t) => {
  if (tabId == null || !src) return null;
  try {
    const resp = await fetch(src);
    if (!resp.ok) return null;
    const clen = Number(resp.headers.get('content-length') || 0);
    if (clen && clen > 25_000_000) return null;
    const dataUrl = await blobToDataUrl(await resp.blob());
    const target = { tabId };
    if (frameId != null) target.frameIds = [frameId];
    const [res] = await chrome.scripting.executeScript({
      target,
      args: [dataUrl, t || 0, FRAME_MAX_SIDE],
      func: async (durl, time, maxSide) => {
        const url = URL.createObjectURL(await (await fetch(durl)).blob());
        return await new Promise((resolve) => {
          const v = document.createElement('video');
          v.muted = true; v.preload = 'auto'; v.src = url;
          let done = false;
          const fin = (x) => { if (!done) { done = true; URL.revokeObjectURL(url); resolve(x); } };
          v.addEventListener('loadeddata', () => {
            try {
              v.currentTime = Math.min(time, Math.max(0, (v.duration || time) - 0.01));
            } catch {
              fin(null);
            }
          });
          v.addEventListener('seeked', () => {
            try {
              const s = Math.min(1, maxSide / Math.max(v.videoWidth, v.videoHeight));
              const w = Math.max(1, Math.round(v.videoWidth * s)), h = Math.max(1, Math.round(v.videoHeight * s));
              const c = document.createElement('canvas');
              c.width = w; c.height = h;
              c.getContext('2d').drawImage(v, 0, 0, w, h);
              fin(c.toDataURL('image/jpeg', 0.92));
            } catch {
              fin(null);
            }
          });
          v.addEventListener('error', () => fin(null));
          setTimeout(() => fin(null), 8000);
        });
      }
    });
    return (res && res.result) || null;
  } catch {
    return null;
  }
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

  // ── Preview submenu: act on the video's POSTER (a normal image URL), never a
  // frame. A no-op when the right-clicked element had no poster. ──
  if (typeof info.menuItemId === 'string' && info.menuItemId.startsWith('stencil-preview-')) {
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
      const dataUrl = await fetchAsDataUrl(act.src);
      const payload = { dataUrl, name: filenameFromUrl(act.src), page: { size: page }, source: act.src, resource, incognito: act.incognito };
      if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId });
      else await openEditorTab(payload);
    } catch (err) {
      console.error('[stencil] preview action failed:', err);
    }
    return;
  }

  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  let src = resolveSrc(info, rec);

  // Video path (Chrome says so, probe saw one, or we have a captured frame). Unless
  // the frame is already in hand: capture in-page at click time, then re-fetch bytes,
  // then screenshot-crop. A media URL is NEVER used as the image (.mp4 won't decode).
  // sourceUrl is captured here for provenance BEFORE the block below overwrites `src`
  // with a frame data URL; for a video it's the media URL. recordOpened ignores non-http.
  const sourceUrl = src || '';
  const resource = tab?.url || '';

  const vinfo = tabId != null ? lastVideoByTab.get(tabId) : null;
  const poster = (tabId != null ? lastPosterByTab.get(tabId) : '') || '';
  const isVideo = info.mediaType === 'video' || !!vinfo || !!(rec && rec.video);
  if (isVideo && !(rec && rec.video && rec.url)) {
    const frameId = vinfo ? vinfo.frameId : info.frameId;
    const probe = await captureVideoFrameInTab(tabId, frameId, vinfo && vinfo.point);
    let frame = probe && probe.frame ? probe.frame : null;
    if (!frame && probe && probe.src) frame = await captureVideoFrameViaFetch(tabId, frameId, probe.src, probe.t);
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
  if (!act) return;
  try {
    if (act.action === 'crop') {
      await launchCrop({ src: act.src, source: sourceUrl, resource, tabId: tab?.id });   // small in-page modal
      return;
    }
    const { page } = await getSettings();
    const dataUrl = await fetchAsDataUrl(act.src);
    // `act.open` ('resume') only set by the Resume item; undefined drops out of the
    // JSON payload so a plain open imports fresh, as before.
    const payload = { dataUrl, name: filenameFromUrl(act.src), page: { size: page }, source: sourceUrl, resource, incognito: act.incognito, open: act.open };
    if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId: tab?.id });   // in-page editor modal
    else await openEditorTab(payload);
  } catch (err) {
    console.error('[stencil] context-menu action failed:', err);
  }
});
