// ── Background service worker ───────────────────────────────────────────────
// Owns the right-click context menu (Stencil actions → editor). Covers real <img>
// (native 'image' context) and CSS background-image elements (detected by the
// content-script probe, ctxTarget.js).
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, blobToDataUrl, buildHandoff, editorOriginPattern } from '../lib/stencil.js';
import { MENU, MENU_ITEMS, resolveContextAction, DYNAMIC_ITEMS, PREVIEW_ITEMS, PIN_ITEMS, STATIC_DESKTOP_ITEMS, pinItemTitle } from '../lib/contextMenu.js';
import { buildStencilSchemeUrl, INLINE_MAX_CHARS } from '../lib/openIn.js';
import { pruneLedger } from '../lib/ledger.js';
import { mountDropZones, unmountDropZones } from '../lib/dropZones.js';
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from '../lib/highlightColor.js';
import { setPinned, loadPins, isPinnedIn, siteOf, PINS_KEY } from '../lib/pins.js';
import { MSG } from '../lib/messages.js';
import { applyAccentActionIcon, watchAccentActionIcon } from '../lib/actionIcon.js';

// Rebuild the menu from scratch; removeAll first avoids "duplicate id" on repeated
// builds. onInstalled/onStartup aren't reliable per reload, so this also runs at
// top level on each worker start (below).
const buildMenus = () => {
  chrome.contextMenus.removeAll(() => {
    for (const item of MENU_ITEMS) chrome.contextMenus.create(item, () => void chrome.runtime.lastError);
    console.info(`[stencil] context menu built: ${MENU_ITEMS.length} items`);
    // Reveal the desktop-app items only if a scheme is configured (they're created hidden).
    syncDesktopMenuVisibility();
  });
};

// Whether a desktop URL scheme is configured — gates the "Open in desktop app" menu items
// (they no-op without a scheme, so hide them). Cached for the synchronous CTX probe handler.
let desktopSchemeSet = true;

// Show/hide the desktop-app hand-off items to match the configured scheme. The STATIC items
// (image / video-frame) toggle on the scheme alone; MENU.bgDesktop is revealed by the probe
// (CTX handler) gated on this flag, so it isn't touched here.
const syncDesktopMenuVisibility = async () => {
  try { const { desktopScheme } = await getSettings(); desktopSchemeSet = !!desktopScheme; }
  catch { desktopSchemeSet = true; }
  for (const id of STATIC_DESKTOP_ITEMS)
    chrome.contextMenus.update(id, { visible: desktopSchemeSet }, () => void chrome.runtime.lastError);
};

// Build immediately on worker startup (covers reloads where onInstalled/onStartup
// don't fire). Idempotent: removeAll precedes every create. buildMenus() also syncs the
// desktop-item visibility once the items exist.
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
// Editor is cross-origin: its project registry lives in its own localStorage,
// unreadable here. A content script injected ONLY into the configured editor origin
// reads that registry (same-origin) and reports it back to prune opened-ledger
// entries for deleted projects. Registration follows the editorUrl setting (refreshed
// on change); already-open editor tabs are injected on startup (no reload needed).
const BRIDGE_ID = 'stencil-editor-bridge';
const BRIDGE_FILE = 'src/content/editorBridge.js';

// `${origin}/*` match pattern for the configured editor comes from lib/stencil.js
// (shared with resumeInOpenEditor), imported above.

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
// When enabled, inject two scripts into every page: a MAIN-world script defining
// window.stencil (entries hold live DOM elements) and an ISOLATED bridge relaying
// the API's action requests to this worker. Off by default; (un)registered as the
// setting flips.
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
  if (changes.desktopScheme) syncDesktopMenuVisibility();   // reveal/hide the desktop-app items
});

chrome.runtime.onInstalled.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
  setUpEditorBridge();
  setUpPageApi();
  applyAccentActionIcon();
});
chrome.runtime.onStartup.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
  setUpEditorBridge();
  setUpPageApi();
  applyAccentActionIcon();
});

// Also set up on every worker start (onInstalled/onStartup don't fire on every wake).
setUpEditorBridge();
registerPageApi();   // re-asserts registration (injection into open tabs only on explicit toggle/startup)
applyAccentActionIcon();   // tint the toolbar icon's outline to the saved accent
watchAccentActionIcon();   // …and re-tint it whenever the accent changes

// What the probe last resolved under the cursor, per tab (ready { url } for a
// background or captured frame). Needed because info.srcUrl is absent (backgrounds)
// or wrong (a <video>'s media file, not a frame).
const lastTargetByTab = new Map();
// Last right-click <video> target, per tab: { frameId, point, rect, dpr }. Lets the
// click handler recapture in-page and screenshot-crop the right rect for tainted media.
const lastVideoByTab = new Map();
// Poster URL of the last right-clicked <video>, per tab — drives the Preview submenu.
const lastPosterByTab = new Map();

// In-memory snapshot of the pinned store, kept fresh from storage. The context-menu
// probe relabels the Pin item (Pin ↔ Unpin) on right-click; that must be SYNCHRONOUS to
// beat the native menu appearing, so it reads this cache instead of awaiting loadPins().
let pinsCache = [];
const refreshPinsCache = async () => { try { pinsCache = await loadPins(); } catch { /* keep the last snapshot */ } };
refreshPinsCache();
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[PINS_KEY]) pinsCache = Array.isArray(changes[PINS_KEY].newValue) ? changes[PINS_KEY].newValue : [];
});
chrome.tabs.onRemoved.addListener((tabId) => {
  lastTargetByTab.delete(tabId);
  lastVideoByTab.delete(tabId);
  lastPosterByTab.delete(tabId);
});

// ── Runtime-message dispatch ────────────────────────────────────────────────
// One handler per message `type` (keyed by MSG.*), replacing a long if/else chain.
// Each is fire-and-forget: none returns true, so — exactly as before — the listener
// leaves the response port closed (no handler here sends an async sendResponse).
const messageHandlers = {
  // The in-page editor overlay asks us to open a real tab when its iframe is
  // blocked (CSP / mixed content). Doing it here avoids popup blockers.
  [MSG.OPEN_TAB]: (msg) => {
    if (msg.url) chrome.tabs.create({ url: msg.url });
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
        const dataUrl = msg.dataUrl || await fetchAsDataUrl(msg.url);
        const { page } = await getSettings();
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
    const resource = msg.resource || sender.tab?.url || '';
    setPinned({
      source: msg.source || msg.url || '', site: siteOf(resource), resource,
      name: msg.name || filenameFromUrl(msg.url || 'image'), kind: msg.kind || 'image', pinned: !!msg.pin,
    }).catch(() => { /* storage unavailable */ });
  },
  // Open a page image/video in the quick-crop tool.
  [MSG.PAGE_CROP]: (msg, sender) => {
    const src = msg.dataUrl || msg.url;
    if (src) launchCrop({ src, source: msg.source || msg.url || '', resource: msg.resource || sender.tab?.url || '', tabId: sender.tab?.id });
  },
  // The API's `stencil.enabled = false` — turn the feature off (unregisters the scripts).
  [MSG.PAGE_DISABLE]: () => {
    chrome.storage.sync.set({ exposeWindowStencil: false });
  },
  // A row drag started in the panel → inject the on-page 4-quadrant drop overlay on that tab,
  // tinted to the current theme accent (resolved from the saved accent key, so the zones match
  // the extension's theme rather than a fixed violet).
  [MSG.DROPZONES_ARM]: (msg) => {
    if (msg.tabId == null) return;
    (async () => {
      let accent = DEFAULT_HL;
      try { const l = await chrome.storage.local.get(ACCENT_STORAGE_KEY); accent = ACCENT_HEX[l[ACCENT_STORAGE_KEY]] || DEFAULT_HL; } catch { /* default */ }
      chrome.scripting.executeScript({ target: { tabId: msg.tabId }, world: 'ISOLATED', func: mountDropZones, args: [accent] })
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
        const { page } = await getSettings();
        if (action === 'crop') {
          const src = await fetchAsDataUrl(url).catch(() => url);   // video/non-image → let the crop page report it
          await launchCrop({ src, source: url, resource, tabId });
          return;
        }
        // here / incognito → open the editor with the image bytes.
        const dataUrl = await fetchAsDataUrl(url);
        const payload = buildHandoff({ name, source: url }, { dataUrl, page, resource, incognito: action === 'incognito' });
        if (action === 'incognito') await openEditorTab(payload);   // incognito = a fresh incognito editor tab
        else await launchEditorModal({ ...payload, tabId });        // here = in-page editor modal
      } catch (err) { console.warn('[stencil] page drop failed:', err?.message); }
    })();
  },
  // ctxTarget probe → what the right-click resolved under the cursor. Records the target
  // per tab and toggles the dynamic background / video-preview menu groups' visibility.
  [MSG.CTX]: (msg, sender) => {
    const tabId = sender.tab?.id;
    if (tabId == null) return;
    const data = msg.data;
    // Remember the video context so the click handler can recapture in-page.
    lastVideoByTab.set(tabId, data && data.video
      ? { frameId: sender.frameId, point: msg.point || null, rect: data.rect || null, dpr: data.dpr || 1, posterShown: !!data.posterShown }
      : null);
    // A tainted (cross-origin) video has no ready frame here; its CURRENT frame is
    // captured at click time (in-page CORS readback → extension byte re-fetch →
    // screenshot crop), so record nothing and let the click handler do the work.
    lastTargetByTab.set(tabId, (data && data.video && !data.url) ? null : (data || null));
    // The poster (preview image) the probe saw, if any — drives the Preview submenu.
    lastPosterByTab.set(tabId, (data && data.poster) ? data.poster : '');
    // Reveal dynamic background/link items only when the probe found a plain image URL
    // (not a <video>); hidden otherwise. <img>/<video> use native-context items,
    // untouched by this toggle.
    const showBg = !!(data && data.url && !data.video);
    for (const id of DYNAMIC_ITEMS)
      chrome.contextMenus.update(id, { visible: showBg }, () => void chrome.runtime.lastError);
    // The background "Open in desktop app" item needs BOTH a background under the cursor AND a
    // configured scheme (unlike the rest of the bg group, which only needs the background).
    chrome.contextMenus.update(MENU.bgDesktop, { visible: showBg && desktopSchemeSet }, () => void chrome.runtime.lastError);
    // Reveal the video Preview submenu only when the probed <video> has a poster, so a
    // posterless video no longer shows a submenu whose actions would be a silent no-op.
    const showPreview = !!(data && data.video && data.poster);
    for (const id of PREVIEW_ITEMS)
      chrome.contextMenus.update(id, { visible: showPreview }, () => void chrome.runtime.lastError);
    // Relabel the pin item to reflect whether the source under the cursor is already pinned
    // on this site (Pin ↔ Unpin). SYNCHRONOUS off the in-memory pins cache — an awaited
    // storage read would lose the race against the native menu appearing and show the prior
    // label. Still best-effort: a first right-click just after the worker wakes may be stale.
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

chrome.runtime.onMessage.addListener((msg, sender) => {
  const handler = msg && messageHandlers[msg.type];
  if (handler) return handler(msg, sender);
});

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

// Capture a <video>'s current frame by in-page canvas readback at click time: direct
// draw (same-origin), then a fresh crossOrigin="anonymous" video at the same src/time
// (works when the CDN serves CORS though the page's <video> is tainted). Returns
// { frame } (JPEG data URL), { src, t } (tainted; caller re-fetches), or null.
const captureVideoFrameInTab = async (tabId, frameId, point) => {
  if (tabId == null) return null;
  const target = { tabId };
  if (frameId != null) target.frameIds = [frameId];
  try {
    const [res] = await chrome.scripting.executeScript({
      target,
      args: [point ? point.x : null, point ? point.y : null, FRAME_MAX_SIDE],
      func: async (px, py, maxSide) => {
        // Smallest <video> whose box contains the cursor — spatially correct even under
        // an overlay, and (unlike querySelector('video') on an ancestor) never jumps to
        // another video.
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

// Current-frame capture for a tainted, non-CORS video: fetch bytes with the extension's
// host permissions (bypasses page CORS), ship them into the page as a blob URL, draw
// the frame at the recorded time. Skips huge media (caller falls back to a screenshot
// crop). Returns a JPEG data URL or null.
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

// Resolve the image source for a click. A probe-captured video frame (rec.video) wins:
// for a <video> info.srcUrl is the media file (not a frame) and Chrome doesn't always
// report mediaType:'video'. Otherwise <img>/<svg> use info.srcUrl, backgrounds use rec.
const resolveSrc = (info, rec) => {
  if (rec && rec.video && rec.url) return rec.url;
  if (info.mediaType === 'video' || info.mediaType === 'audio') return (rec && rec.url) || null;
  if (info.srcUrl) return info.srcUrl;
  return (rec && rec.url) || null;
};

// ── Context-menu click handlers ─────────────────────────────────────────────
// One async handler per click group (following the ACTIONS-table style): the toolbar
// action items, the video-preview submenu, the pin items, and the default image / video
// frame path. `resolveClickHandler` routes an incoming click to exactly one of them —
// the same order the previous if/else chain matched in.

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
    const dataUrl = await fetchAsDataUrl(act.src);
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

// ── "Open in desktop app" (context menu) → hand the image to the desktop app via its
// stencil:// URL scheme, with the bytes embedded inline (a page image has no server project,
// unlike the popup's shared rows). Opened with chrome.tabs.create so the OS external-protocol
// prompt fires from the SW (no popup/user-gesture document to rely on). Oversized inline
// payloads are refused (the OS launch machinery can't carry them). ──
const openInDesktopFromMenu = async (src) => {
  const { desktopScheme } = await getSettings();
  if (!desktopScheme || !src) return;
  try {
    const dataUrl = await fetchAsDataUrl(src);
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

  // Video path (Chrome says so, probe saw one, or frame in hand). Unless the frame is
  // already in hand: capture in-page at click time → re-fetch bytes → screenshot-crop.
  // A media URL is NEVER used as the image (.mp4 won't decode). sourceUrl captured here
  // for provenance BEFORE the block below overwrites `src` with a frame data URL; for a
  // video it's the media URL. recordOpened ignores non-http.
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
    if (act.action === 'desktop') { await openInDesktopFromMenu(act.src); return; }
    const { page } = await getSettings();
    const dataUrl = await fetchAsDataUrl(act.src);
    // `act.open` ('resume') only set by the Resume item; undefined drops out of the
    // JSON payload so a plain open imports fresh, as before.
    const payload = buildHandoff({ name: filenameFromUrl(act.src), source: sourceUrl }, { dataUrl, page, resource, incognito: act.incognito, open: act.open });
    if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId: tab?.id });   // in-page editor modal
    else await openEditorTab(payload);
  } catch (err) {
    console.error('[stencil] context-menu action failed:', err);
  }
};

// Route a click to its handler, matching in the same order the old if/else chain did:
// the two toolbar action items, then the preview-* submenu, then the pin items, then
// the default image / video-frame path.
const resolveClickHandler = (info) => {
  if (info.menuItemId === MENU.actionOpen || info.menuItemId === MENU.actionOpenIncognito) return openFreshEditor;
  if (typeof info.menuItemId === 'string' && info.menuItemId.startsWith('stencil-preview-')) return actOnPreview;
  if (PIN_ITEMS.includes(info.menuItemId)) return togglePinFromMenu;
  return openImageOrFrame;
};

chrome.contextMenus.onClicked.addListener((info, tab) => resolveClickHandler(info)(info, tab, tab?.id));
