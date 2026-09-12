// ── Image bytes: fetch, encode, name ────────────────────────────────────────
// Service-worker-safe (no FileReader / DOM). Every fetch here goes through
// lib/urlGuard.js — these URLs are harvested from arbitrary pages.
import { isAllowedImageUrl } from './urlGuard.js';

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

// Encode a Blob as a data URL without FileReader (so it works in the service worker too).
export const blobToDataUrl = async (blob) =>
  `data:${blob.type || 'image/jpeg'};base64,${arrayBufferToBase64(await blob.arrayBuffer())}`;
