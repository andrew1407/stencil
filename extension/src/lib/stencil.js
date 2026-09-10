// ── Shared extension helpers ────────────────────────────────────────────────
// The pure helpers (buildLaunchUrl, filenameFromUrl, guessMime) are unit-tested;
// the rest wrap chrome.* and are service-worker-safe (no FileReader / DOM).
import { mountStencilModal } from './overlay.js';
import { loadShellTheme } from './shellTheme.js';
import { recordOpened } from './ledger.js';
import { sourceOf } from './imageModel.js';
import { isAllowedImageUrl } from './urlGuard.js';
import { MSG } from './messages.js';

export const DEFAULT_EDITOR_URL = 'http://localhost:8080/';
export const DEFAULT_PAGE = 'A3';
// Default desktop-app URL scheme for the "Open in…" desktop hand-off (mirrors the browser
// app's openInConfig `desktopScheme`). Empty = hide the Desktop action.
const DEFAULT_DESKTOP_SCHEME = 'stencil';

// Settings live in chrome.storage.sync so they follow the user across machines.
export const getSettings = async () => {
  const s = await chrome.storage.sync.get({ editorUrl: DEFAULT_EDITOR_URL, page: DEFAULT_PAGE, markOpened: true, openedFirst: true, showPinned: true, hoverHighlight: false, highlightColor: 'theme', exposeWindowStencil: false, editorPageApi: true, desktopScheme: DEFAULT_DESKTOP_SCHEME, telegramBotUsername: '' });
  return {
    editorUrl: (s.editorUrl || DEFAULT_EDITOR_URL).trim() || DEFAULT_EDITOR_URL,
    page: s.page || DEFAULT_PAGE,
    // "Open in…" targets, mirroring the browser app's openInConfig. The desktop app's OS
    // URL scheme (default 'stencil'; empty hides the Desktop action) and the Telegram bot's
    // username (empty hides the Telegram action). Local operator config — see options.
    desktopScheme: typeof s.desktopScheme === 'string' ? s.desktopScheme.trim() : DEFAULT_DESKTOP_SCHEME,
    telegramBotUsername: typeof s.telegramBotUsername === 'string' ? s.telegramBotUsername.trim() : '',
    // The on-page highlight outline colour: 'theme' = follow the main accent, or a hex
    // string for a custom colour. Read live by the popup + page API when highlighting.
    highlightColor: typeof s.highlightColor === 'string' && s.highlightColor ? s.highlightColor : 'theme',
    // Whether the popup badges images that already have an editor (default on).
    markOpened: s.markOpened !== false,
    // Whether the popup sorts opened images to the top (default on). Toggled live from
    // the popup, persisted here. Independent of markOpened, but a no-op when badging is off.
    openedFirst: s.openedFirst !== false,
    // Whether the popup styles pinned images (gray outline) and floats them to the top
    // (default on). Toggled live from the popup; pinning still works when off.
    showPinned: s.showPinned !== false,
    // Whether hovering a list row outlines its element on the page — and, with the on-page
    // highlight on, hovering a page element outlines its row (default OFF). Side panel /
    // DevTools panel only; toggled live from the panel.
    hoverHighlight: s.hoverHighlight === true,
    // Whether to inject a page-global `window.stencil` scripting API into every page
    // (default OFF — touches every page's main world, so strictly opt-in).
    exposeWindowStencil: s.exposeWindowStencil === true,
    // Whether the EDITOR page gets `stencil.extension`. Default ON: unlike exposeWindowStencil
    // it touches exactly one origin — the configured editor, our own front-end.
    editorPageApi: s.editorPageApi !== false
  };
};

export const setSettings = async (patch) => {
  await chrome.storage.sync.set(patch);
};

// `${origin}/*` match pattern for a URL, or null when it isn't an http(s) origin we
// can scope tabs.query / a content script / a permission request to.
export const originPattern = (url) => {
  try {
    const origin = new URL(url).origin;
    return origin.startsWith('http') ? `${origin}/*` : null;
  } catch {
    return null;
  }
};

// originPattern for the configured editor. Shared by the background bridge
// registration and resumeInOpenEditor.
export const editorOriginPattern = async () => {
  try {
    return originPattern((await getSettings()).editorUrl);
  } catch {
    return null;
  }
};

// Bring a tab to the front: select it AND raise its window (two calls — selecting a tab
// in a background window leaves it hidden). Shared so "focus that editor" means one thing.
export const focusTab = async (tab) => {
  await chrome.tabs.update(tab.id, { active: true });
  if (tab.windowId != null) await chrome.windows.update(tab.windowId, { focused: true });
};

// Resume an already-opened image in the editor tab that's ALREADY open: focus that tab
// and ask its (same-origin) editorBridge to switch to the matching project — no
// navigation, nothing lost. False = fall back to opening a new tab.
export const resumeInOpenEditor = async ({ source, name }) => {
  const pattern = await editorOriginPattern();
  if (!pattern) return false;
  try {
    const [tab] = await chrome.tabs.query({ url: [pattern] });
    if (!tab || tab.id == null) return false;
    await focusTab(tab);
    // Throws if no editorBridge is listening (e.g. a very old tab we couldn't inject) →
    // caller falls back to a fresh tab.
    await chrome.tabs.sendMessage(tab.id, { type: MSG.EDITOR_SWITCH, source, name });
    return true;
  } catch {
    return false;
  }
};

// Base64-encode an ArrayBuffer in chunks (no FileReader → works in the SW).
const arrayBufferToBase64 = (buf) => {
  const bytes = new Uint8Array(buf);
  const CHUNK = 0x8000;
  let binary = '';
  for (let i = 0; i < bytes.length; i += CHUNK)
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + CHUNK));
  return btoa(binary);
};

// Fetch any image URL (http(s)/blob:/data:) and return it as a data URL — see
// fetchAsDataUrl below. The extension's host_permissions bypass page CORS → no tainted canvas.
// A data: URL that declares an image — handlers taking a caller-supplied `dataUrl`
// instead of fetching one must still check it. SVG is allowed (the editor renders it).
export const isImageDataUrl = (s) => typeof s === 'string' && /^data:image\/[a-z0-9.+-]+[;,]/i.test(s);

export const fetchAsDataUrl = async (url, { pageUrl = '' } = {}) => {
  // Pass through without a fetch, but only when it declares an image.
  if (url.startsWith('data:')) {
    if (!isImageDataUrl(url)) throw new Error('data: URL is not an image');
    return url;
  }
  // Scheme allowlist: the extension's host_permissions let fetch() reach ANY URL and
  // bypass CORS, so a page-supplied `file:`, `ftp:`, `chrome:` etc. must be refused —
  // only http(s)/blob image URLs are fetched. Mirrors the browser app's deep-link allowlist.
  if (!/^(https?|blob):/i.test(url)) throw new Error('unsupported URL scheme');
  // SSRF guard: these URLs are harvested from pages, so loopback/private/link-local/
  // metadata literals are refused (urlGuard.js). `pageUrl` (TRUSTED — sender.tab.url or
  // a scan-recorded resource, never page-supplied) lets the page's OWN host through.
  if (!isAllowedImageUrl(url, { allowSameHostAs: pageUrl })) throw new Error('blocked private or internal address');
  const resp = await fetch(url);
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  const type = resp.headers.get('content-type') || guessMime(url);
  // Reject a video/audio media URL: an <img> handed a data:video/… URL just
  // "fails to decode" (and skip buffering the whole media file).
  const lc = type.toLowerCase();
  if (lc.startsWith('video/') || lc.startsWith('audio/')) throw new Error('source is video/audio, not an image');
  const buf = await resp.arrayBuffer();
  return `data:${type};base64,${arrayBufferToBase64(buf)}`;
};

export const guessMime = (url) => {
  const m = /\.(png|jpe?g|gif|webp|avif|bmp|ico|tiff?|svg)(?:[?#]|$)/i.exec(url);
  const ext = ((m && m[1]) || 'png').toLowerCase();
  return ({ jpg: 'image/jpeg', jpeg: 'image/jpeg', svg: 'image/svg+xml', ico: 'image/x-icon', tif: 'image/tiff' })[ext] || `image/${ext}`;
};

// Derive a reasonable download / project filename (with extension) from an image URL.
export const filenameFromUrl = (url, fallback = 'image') => {
  try {
    if (url.startsWith('data:')) {
      const mime = /^data:([^;,]+)/.exec(url);
      const ext = mime ? (mime[1].split('/')[1] || 'png').replace('+xml', '') : 'png';
      return `${fallback}.${ext}`;
    }
    const u = new URL(url);
    const base = decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || '');
    if (base && /\.[a-z0-9]{2,4}$/i.test(base)) return base;
    return `${base || fallback}.png`;
  } catch {
    return `${fallback}.png`;
  }
};

// Build the editor launch URL. The image + options ride in the URL fragment
// (`#stencil=…`) so they never reach the server (read in applyExternalLaunch()).
export const buildLaunchUrl = (editorUrl, payload) => {
  const base = editorUrl.split('#')[0];
  return `${base}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;
};

// Assemble the editor/crop hand-off payload — the single shape every surface sends to
// openEditorTab / launchEditorModal: { dataUrl, name, page:{size}, source, resource,
// incognito[, open] }. Shared (server) rows carry their OWN source/resource; page images
// derive source and take the caller's page URL as resource. `open` ('resume'|'copy') is
// omitted when undefined so a plain open imports fresh.
export const buildHandoff = (image, { dataUrl, page, resource, incognito = false, open } = {}) => {
  const payload = {
    dataUrl,
    name: image.name,
    page: { size: page },
    // Provenance: shared rows keep their own source; page images derive it (or use an
    // explicitly pre-resolved image.source, as the background relays pass).
    source: image.shared ? image.source : (image.source ?? sourceOf(image)),
    resource: image.shared ? image.resource : resource,
    incognito: !!incognito,
  };
  if (open !== undefined) payload.open = open;
  return payload;
};

// Soft ceiling: very large data URLs can exceed the URL length limit in a tab.
// Past it Chrome drops the navigation and the editor tab lands on about:blank.
export const MAX_PAYLOAD = 1_800_000;

// Encode a Blob as a data URL without FileReader (so it works in the service worker too).
export const blobToDataUrl = async (blob) =>
  `data:${blob.type || 'image/jpeg'};base64,${arrayBufferToBase64(await blob.arrayBuffer())}`;

const scaleRect = (r, k) => ({
  x: Math.round(r.x * k), y: Math.round(r.y * k),
  width: Math.max(1, Math.round(r.width * k)), height: Math.max(1, Math.round(r.height * k))
});

// Re-encode payload.dataUrl smaller until the launch URL fits MAX_PAYLOAD, so a
// big image (e.g. a 4K video frame) never overflows into about:blank. Any crop
// rect (original-image pixels) is scaled by the same factor.
const fitLaunchPayload = async (editorUrl, payload) => {
  let url = buildLaunchUrl(editorUrl, payload);
  const data = payload.dataUrl;
  if (url.length <= MAX_PAYLOAD || typeof data !== 'string' || !data.startsWith('data:image')) return { payload, url };
  try {
    const bmp = await createImageBitmap(await (await fetch(data)).blob());
    for (let scale = 0.8; scale >= 0.12 && url.length > MAX_PAYLOAD; scale *= 0.8) {
      const w = Math.max(1, Math.round(bmp.width * scale)), h = Math.max(1, Math.round(bmp.height * scale));
      const c = new OffscreenCanvas(w, h);
      c.getContext('2d').drawImage(bmp, 0, 0, w, h);
      const next = { ...payload, dataUrl: await blobToDataUrl(await c.convertToBlob({ type: 'image/jpeg', quality: 0.85 })) };
      if (payload.crop) next.crop = scaleRect(payload.crop, w / bmp.width);
      const nextUrl = buildLaunchUrl(editorUrl, next);
      if (nextUrl.length < url.length) { payload = next; url = nextUrl; }
    }
  } catch {
    /* keep the original; the caller still warns below */
  }
  return { payload, url };
};

// Build the editor launch URL, shrinking the image if needed to stay under the length
// limit. Shared by the tab and in-page-modal launchers. `source`/`resource` = the
// image's own URL + its page; `open` ('resume'|'copy') switches to a matching project.
const buildEditorLaunchUrl = async (payload) => {
  const { editorUrl } = await getSettings();
  const fitted = await fitLaunchPayload(editorUrl, payload);
  if (fitted.url.length > MAX_PAYLOAD)
    console.warn(`[stencil] launch URL is ${fitted.url.length} bytes — image may be too large.`);
  return fitted.url;
};

// Append a hand-off to the opened-images ledger (best-effort) so the popup can badge
// this image as already-opened. No-op for incognito launches (editor never persists
// them) and untrackable sources (handled in recordOpened).
const noteOpened = async (payload) => {
  if (payload.incognito) return;
  const { editorUrl } = await getSettings();
  await recordOpened({ source: payload.source, resource: payload.resource, name: payload.name, editorUrl });
};

// Open the full editor in a NEW browser tab with the given image payload. The editor's
// own multi-project / cross-tab UI surfaces any already-open editors.
export const openEditorTab = async (payload) => {
  const tab = await chrome.tabs.create({ url: await buildEditorLaunchUrl(payload) });
  await noteOpened(payload);
  return tab;
};

// Open the full editor as a small in-page modal on the given tab (mirrors launchCrop).
// Falls back to a real tab when `tabId` is null, the modal can't be injected
// (restricted page), or the editor frame is later CSP-blocked.
export const launchEditorModal = async ({ tabId, ...payload }) => {
  const url = await buildEditorLaunchUrl(payload);
  if (tabId == null) {
    const tab = await chrome.tabs.create({ url });
    await noteOpened(payload);
    return tab;
  }
  const title = payload.incognito ? 'Stencil editor (incognito)' : 'Stencil editor';
  try {
    await chrome.scripting.executeScript({
      // The shell can't read our CSS variables from inside someone else's page, so the
      // user's Appearance + accent choice travels with it as data (lib/shellTheme.js).
      target: { tabId }, world: 'ISOLATED', func: mountStencilModal, args: [url, title, 8000, await loadShellTheme()]
    });
    await noteOpened(payload);
  } catch {
    return chrome.tabs.create({ url });
  }
};

// The crop page reads its image from session storage under this key — not the
// URL, since a captured video frame is a data URL hundreds of KB long that a
// query string would truncate. Storage is shared between the SW and the crop page.
export const CROP_SRC_KEY = 'stencil-crop-src';
// Provenance (source/resource URLs) for the image being cropped, threaded through
// to the post-crop editor hand-off so a cropped image keeps where it came from.
export const CROP_META_KEY = 'stencil-crop-meta';

// Open the quick-crop tool as a small in-page modal on the given tab (falls back to a
// real tab when the modal can't be injected / the frame is CSP-blocked). `source`/
// `resource` ride along via session storage so the cropped result keeps its provenance.
export const launchCrop = async ({ src, source, resource, tabId }) => {
  try {
    await chrome.storage.session.set({ [CROP_SRC_KEY]: src, [CROP_META_KEY]: { source: source || '', resource: resource || '' } });
  } catch {
    /* crop page shows a message */
  }
  const url = chrome.runtime.getURL('src/crop/crop.html');
  if (tabId == null) return chrome.tabs.create({ url });
  try {
    // Explicit ready timeout, matching the editor modal's: the watchdog only catches a
    // frame the page's CSP blocked outright, so it must not race the crop page's own
    // load (a short one would close a WORKING modal and re-open the crop page in a tab).
    await chrome.scripting.executeScript({
      target: { tabId }, world: 'ISOLATED', func: mountStencilModal, args: [url, 'Quick crop', 8000, await loadShellTheme()]
    });
  } catch {
    return chrome.tabs.create({ url });
  }
};
