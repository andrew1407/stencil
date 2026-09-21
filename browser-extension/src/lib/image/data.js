// Service-worker-safe (no FileReader / DOM). Every fetch goes through lib/urlGuard.js —
// these URLs are harvested from arbitrary pages.
import { isAllowedImageUrl } from '../connection/urlGuard.js';

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

// The host_permissions bypass page CORS, so the result never taints a canvas.
export const fetchAsDataUrl = async (url, { pageUrl = '' } = {}) => {
  if (url.startsWith('data:')) {
    if (!isImageDataUrl(url)) throw new Error('data: URL is not an image');
    return url;
  }
  // host_permissions let fetch() reach ANY URL, so a page-supplied file:/ftp:/chrome: must
  // be refused. Mirrors the browser app's deep-link allowlist.
  if (!/^(https?|blob):/i.test(url)) throw new Error('unsupported URL scheme');
  // `pageUrl` is TRUSTED (sender.tab.url or a scan-recorded resource, never page-supplied).
  if (!isAllowedImageUrl(url, { allowSameHostAs: pageUrl })) throw new Error('blocked private or internal address');
  const resp = await fetch(url);
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  const type = resp.headers.get('content-type') || guessMime(url);
  // An <img> handed a data:video/… URL just "fails to decode" — refuse before buffering it.
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
