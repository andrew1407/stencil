// Service-worker-safe (no FileReader / DOM). Every fetch goes through connection/urlGuard.js —
// these URLs are harvested from arbitrary pages.
import { guardedFetch, readCapped } from '../connection/urlGuard.js';

const arrayBufferToBase64 = (buf) => {
  const bytes = new Uint8Array(buf);
  const CHUNK = 0x8000;
  let binary = '';
  for (let i = 0; i < bytes.length; i += CHUNK)
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + CHUNK));
  return btoa(binary);
};

// Handlers taking a caller-supplied `dataUrl` must still check it. SVG is allowed.
export const isImageDataUrl = (s) => typeof s === 'string' && /^data:image\/[a-z0-9.+-]+[;,]/i.test(s);

// The raster signatures a CDN may serve under a generic content type (application/octet-stream).
const SIGNATURES = [
  ['image/png', [0x89, 0x50, 0x4e, 0x47]], ['image/jpeg', [0xff, 0xd8, 0xff]], ['image/gif', [0x47, 0x49, 0x46, 0x38]],
  ['image/bmp', [0x42, 0x4d]], ['image/x-icon', [0x00, 0x00, 0x01, 0x00]],
  ['image/tiff', [0x49, 0x49, 0x2a, 0x00]], ['image/tiff', [0x4d, 0x4d, 0x00, 0x2a]],
];
export const sniffImageMime = (buf) => {
  const b = new Uint8Array(buf, 0, Math.min(buf.byteLength, 16));
  const at = (sig, off = 0) => sig.every((v, i) => b[off + i] === v);
  if (at([0x52, 0x49, 0x46, 0x46]) && at([0x57, 0x45, 0x42, 0x50], 8)) return 'image/webp';
  if (at([0x66, 0x74, 0x79, 0x70], 4) && at([0x61, 0x76, 0x69], 8)) return 'image/avif';
  return SIGNATURES.find(([, sig]) => at(sig))?.[0] || '';
};

// The host_permissions bypass page CORS, so the result never taints a canvas. `imagesOnly` (a
// page-API request, whose caller could read the bytes back) refuses anything that is not an image:
// an image/* type, or bytes with an image signature under another type.
export const fetchAsDataUrl = async (url, { pageUrl = '', imagesOnly = false } = {}) => {
  if (url.startsWith('data:')) {
    if (!isImageDataUrl(url)) throw new Error('data: URL is not an image');
    return url;
  }
  // host_permissions let fetch() reach ANY URL, so a page-supplied file:/ftp:/chrome: must
  // be refused. Mirrors the browser app's deep-link allowlist.
  if (!/^(https?|blob):/i.test(url)) throw new Error('unsupported URL scheme');
  // `pageUrl` is TRUSTED (sender.tab.url or a scan-recorded resource, never page-supplied).
  const resp = await guardedFetch(url, { allowSameHostAs: pageUrl });
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  const type = resp.headers.get('content-type') || guessMime(url);
  // An <img> handed a data:video/… URL just "fails to decode" — refuse before buffering it.
  const lc = type.toLowerCase();
  if (lc.startsWith('video/') || lc.startsWith('audio/')) throw new Error('source is video/audio, not an image');
  const buf = await readCapped(resp);
  if (imagesOnly && !lc.startsWith('image/')) {
    const sniffed = sniffImageMime(buf);
    if (!sniffed) throw new Error('source is not an image');
    return `data:${sniffed};base64,${arrayBufferToBase64(buf)}`;
  }
  return `data:${type};base64,${arrayBufferToBase64(buf)}`;
};

export const guessMime = (url) => {
  const m = /\.(png|jpe?g|gif|webp|avif|bmp|ico|tiff?|svg)(?:[?#]|$)/i.exec(url);
  const ext = ((m && m[1]) || 'png').toLowerCase();
  return ({ jpg: 'image/jpeg', jpeg: 'image/jpeg', svg: 'image/svg+xml', ico: 'image/x-icon', tif: 'image/tiff' })[ext] || `image/${ext}`;
};

export const filenameFromUrl = (url, fallback = 'image') => {
  try {
    if (url.startsWith('data:')) {
      const mime = /^data:([^;,]+)/.exec(url);
      const ext = mime ? (mime[1].split('/')[1] || 'png').replace('+xml', '') : 'png';
      return `${fallback}.${ext}`;
    }
    const u = new URL(url);
    // Decoded, so a %2e%2e%2f segment becomes real separators: strip them and any control
    // byte before this reaches chrome.downloads.download as a filename.
    const base = decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || '')
      .replace(/[\\/\x00-\x1f]/g, '_');
    if (base && /\.[a-z0-9]{2,4}$/i.test(base)) return base;
    return `${base || fallback}.png`;
  } catch {
    return `${fallback}.png`;
  }
};

// No FileReader, so it works in the service worker too.
export const blobToDataUrl = async (blob) =>
  `data:${blob.type || 'image/jpeg'};base64,${arrayBufferToBase64(await blob.arrayBuffer())}`;
