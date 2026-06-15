// ── Shared extension helpers ────────────────────────────────────────────────
// The pure helpers (buildLaunchUrl, filenameFromUrl, guessMime) are unit-tested;
// the rest wrap chrome.* and are service-worker-safe (no FileReader / DOM).
import { mountStencilModal } from './overlay.js';

export const DEFAULT_EDITOR_URL = 'http://localhost:8080/';
export const DEFAULT_PAGE = 'A3';

// Settings live in chrome.storage.sync so they follow the user across machines.
export async function getSettings() {
  const s = await chrome.storage.sync.get({ editorUrl: DEFAULT_EDITOR_URL, page: DEFAULT_PAGE });
  return {
    editorUrl: (s.editorUrl || DEFAULT_EDITOR_URL).trim() || DEFAULT_EDITOR_URL,
    page: s.page || DEFAULT_PAGE
  };
}

export async function setSettings(patch) {
  await chrome.storage.sync.set(patch);
}

// Base64-encode an ArrayBuffer in chunks (no FileReader → works in the SW).
function arrayBufferToBase64(buf) {
  const bytes = new Uint8Array(buf);
  const CHUNK = 0x8000;
  let binary = '';
  for (let i = 0; i < bytes.length; i += CHUNK)
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + CHUNK));
  return btoa(binary);
}

// Fetch any image URL (http(s)/blob:/data:) and return it as a data URL. The
// extension's host_permissions bypass page CORS → the editor never sees a
// tainted canvas.
export async function fetchAsDataUrl(url) {
  if (url.startsWith('data:')) return url;
  const resp = await fetch(url);
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  const buf = await resp.arrayBuffer();
  const type = resp.headers.get('content-type') || guessMime(url);
  return `data:${type};base64,${arrayBufferToBase64(buf)}`;
}

export function guessMime(url) {
  const m = /\.(png|jpe?g|gif|webp|avif|bmp|ico|tiff?|svg)(?:[?#]|$)/i.exec(url);
  const ext = ((m && m[1]) || 'png').toLowerCase();
  return ({ jpg: 'image/jpeg', jpeg: 'image/jpeg', svg: 'image/svg+xml', ico: 'image/x-icon', tif: 'image/tiff' })[ext] || `image/${ext}`;
}

// A reasonable download / project filename from the image URL.
export function filenameFromUrl(url, fallback = 'image') {
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
}

// Build the editor launch URL. Image + options ride in the URL fragment
// (`#stencil=…`) so they never reach the server. Editor reads it in
// DrawingApp.applyExternalLaunch().
export function buildLaunchUrl(editorUrl, payload) {
  const base = editorUrl.split('#')[0];
  return `${base}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;
}

// Soft ceiling: very large data URLs can exceed the URL length limit in a tab.
export const MAX_PAYLOAD = 1_800_000;

// Open the full editor in a NEW browser tab with the given image payload.
//   payload = { dataUrl, name?, crop?, page?, incognito? }
// The editor's own multi-project / cross-tab UI surfaces any already-open editors.
export async function openEditorTab(payload) {
  const { editorUrl } = await getSettings();
  const url = buildLaunchUrl(editorUrl, payload);
  if (url.length > MAX_PAYLOAD)
    console.warn(`[stencil] launch URL is ${url.length} bytes — image may be too large.`);
  return chrome.tabs.create({ url });
}

// Build the URL of the quick-crop tool for an image.
export function cropUrl(src) {
  return chrome.runtime.getURL(`src/crop/crop.html?src=${encodeURIComponent(src)}`);
}

// Open the quick-crop tool as a small in-page MODAL on the given tab (so the
// user stays put). Falls back to a real tab when the modal can't be injected
// (restricted page) or when the frame is later blocked by the page's CSP.
export async function launchCrop({ src, tabId }) {
  const url = cropUrl(src);
  if (tabId == null) return chrome.tabs.create({ url });
  try {
    await chrome.scripting.executeScript({
      target: { tabId }, world: 'ISOLATED', func: mountStencilModal, args: [url, 'Quick crop']
    });
  } catch {
    return chrome.tabs.create({ url });
  }
}
