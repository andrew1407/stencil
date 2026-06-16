// ── Shared extension helpers ────────────────────────────────────────────────
// The pure helpers (buildLaunchUrl, filenameFromUrl, guessMime) are unit-tested;
// the rest wrap chrome.* and are service-worker-safe (no FileReader / DOM).
import { mountStencilModal } from './overlay.js';

export const DEFAULT_EDITOR_URL = 'http://localhost:8080/';
export const DEFAULT_PAGE = 'A3';

// Settings live in chrome.storage.sync so they follow the user across machines.
export const getSettings = async () => {
  const s = await chrome.storage.sync.get({ editorUrl: DEFAULT_EDITOR_URL, page: DEFAULT_PAGE });
  return {
    editorUrl: (s.editorUrl || DEFAULT_EDITOR_URL).trim() || DEFAULT_EDITOR_URL,
    page: s.page || DEFAULT_PAGE
  };
};

export const setSettings = async (patch) => {
  await chrome.storage.sync.set(patch);
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

// Fetch any image URL (http(s)/blob:/data:) and return it as a data URL. The
// extension's host_permissions bypass page CORS → no tainted canvas.
export const fetchAsDataUrl = async (url) => {
  if (url.startsWith('data:')) return url;
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

// A reasonable download / project filename from the image URL.
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

// Build the editor launch URL. Image + options ride in the URL fragment
// (`#stencil=…`) so they never reach the server (read in applyExternalLaunch()).
export const buildLaunchUrl = (editorUrl, payload) => {
  const base = editorUrl.split('#')[0];
  return `${base}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;
};

// Soft ceiling: very large data URLs can exceed the URL length limit in a tab.
// Past it Chrome drops the navigation and the editor tab lands on about:blank.
export const MAX_PAYLOAD = 1_800_000;

// Encode a Blob as a data URL (no FileReader → works in the service worker too).
const blobToDataUrl = async (blob) =>
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
  } catch { /* keep the original; the caller still warns below */ }
  return { payload, url };
};

// Open the full editor in a NEW browser tab with the given image payload.
//   payload = { dataUrl, name?, crop?, page?, incognito? }
// The editor's own multi-project / cross-tab UI surfaces any already-open editors.
export const openEditorTab = async (payload) => {
  const { editorUrl } = await getSettings();
  const fitted = await fitLaunchPayload(editorUrl, payload);
  if (fitted.url.length > MAX_PAYLOAD)
    console.warn(`[stencil] launch URL is ${fitted.url.length} bytes — image may be too large.`);
  return chrome.tabs.create({ url: fitted.url });
};

// The crop page reads its image from session storage under this key — not the
// URL, since a captured video frame is a data URL hundreds of KB long that a
// query string would truncate. Storage is shared between the SW and the crop page.
export const CROP_SRC_KEY = 'stencil-crop-src';

// Open the quick-crop tool as a small in-page modal on the given tab. Falls back
// to a real tab when the modal can't be injected (restricted page) or the frame
// is later blocked by the page's CSP.
export const launchCrop = async ({ src, tabId }) => {
  try { await chrome.storage.session.set({ [CROP_SRC_KEY]: src }); } catch { /* crop page shows a message */ }
  const url = chrome.runtime.getURL('src/crop/crop.html');
  if (tabId == null) return chrome.tabs.create({ url });
  try {
    await chrome.scripting.executeScript({
      target: { tabId }, world: 'ISOLATED', func: mountStencilModal, args: [url, 'Quick crop']
    });
  } catch {
    return chrome.tabs.create({ url });
  }
};
