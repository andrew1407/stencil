// Rasterises any image source to PNG: llm-contract.md §7 accepts png/jpeg/webp/gif only,
// and Chrome's createImageBitmap refuses SVG, so decoding is bitmap-first with an <img>
// at an explicit size as the fallback. Every DOM seam is injected (node --test stubs it).

import { isAllowedImageUrl } from './urlGuard.js';

// Long edge a decoded image is fitted into by default (contract §7 downscale).
export const DEFAULT_MAX_EDGE = 1568;
// Long edge when the source has no usable pixel size (an SVG with only a viewBox), and
// the minimum a vector source is rasterised at — a favicon-sized render is useless to vision.
export const DEFAULT_RASTER_EDGE = 512;
// A stuck <img> load must not hang the chat's send loop.
const DECODE_TIMEOUT_MS = 10000;

export const DECODE_ERROR = 'the image could not be decoded (unsupported or blocked source)';
const BLOCKED_URL = 'blocked private or internal address';

export const isSvgType = (type) => /^image\/svg(\+xml)?$/i.test(String(type || '').split(';')[0].trim());
export const isSvgUrl = (url) => /^data:image\/svg\+xml[;,]/i.test(String(url || ''))
  || /\.svgz?(?:[?#]|$)/i.test(String(url || ''));

// The media type declared by a data: URL ('' for anything else).
export const mediaTypeOf = (url) => {
  const m = /^data:([^;,]+)/i.exec(String(url || ''));
  return m ? m[1].toLowerCase() : '';
};

// Scale w×h down so its long edge fits `maxEdge` (never up). {0,0} when unknown.
export const fitSize = (w, h, maxEdge = DEFAULT_MAX_EDGE) => {
  const W = Number(w) || 0;
  const H = Number(h) || 0;
  if (!(W > 0 && H > 0)) return { width: 0, height: 0 };
  const k = Math.min(1, maxEdge / Math.max(W, H));
  return { width: Math.max(1, Math.round(W * k)), height: Math.max(1, Math.round(H * k)) };
};

// The size policy: known dims, else intrinsic size, else a `fallbackEdge` square — each
// fitted to `maxEdge`; `minEdge` then scales the result up to that long edge.
export const rasterSize = ({
  width = 0, height = 0, naturalWidth = 0, naturalHeight = 0,
  maxEdge = DEFAULT_MAX_EDGE, fallbackEdge = DEFAULT_RASTER_EDGE, minEdge = 0,
} = {}) => {
  const cap = Math.max(1, Math.round(maxEdge));
  let s = fitSize(width, height, cap);
  if (!s.width) s = fitSize(naturalWidth, naturalHeight, cap);
  if (!s.width) {
    const e = Math.max(1, Math.min(cap, Math.round(fallbackEdge)));
    return { width: e, height: e };
  }
  const want = Math.min(cap, Math.round(minEdge) || 0);
  const long = Math.max(s.width, s.height);
  if (want > long) {
    const k = want / long;
    s = { width: Math.max(1, Math.round(s.width * k)), height: Math.max(1, Math.round(s.height * k)) };
  }
  return s;
};

// Built lazily so importing under node --test (no DOM globals) is harmless.
const domDeps = () => ({
  createBitmap: typeof createImageBitmap === 'function' ? (blob) => createImageBitmap(blob) : null,
  makeImage: () => document.createElement('img'),
  makeCanvas: () => document.createElement('canvas'),
  // Sources reach here as data:/blob: (fetchAsDataUrl pulled the bytes under the guard
  // already); anything else is a page-harvested URL, so it takes the same SSRF check.
  toBlob: async (url) => {
    if (!isAllowedImageUrl(url)) throw new Error(BLOCKED_URL);
    return (await fetch(url)).blob();
  },
  objectUrl: (blob) => URL.createObjectURL(blob),
  revokeUrl: (url) => URL.revokeObjectURL(url),
  timer: (fn, ms) => setTimeout(fn, ms),
  clearTimer: (id) => clearTimeout(id),
});

const deps = (extra) => ({ ...domDeps(), ...(extra || {}) });

const drawToPng = (src, { width, height }, d) => {
  const c = d.makeCanvas();
  c.width = width;
  c.height = height;
  const ctx = c.getContext('2d');
  if (!ctx) throw new Error(DECODE_ERROR);
  ctx.drawImage(src, 0, 0, width, height);
  const url = c.toDataURL('image/png');
  if (!/^data:image\/png;base64,/.test(String(url))) throw new Error('could not encode the image as PNG');
  return url;
};

// The explicit width/height is load-bearing: an SVG with no intrinsic size renders at
// the element's box, so the box IS the rasterisation resolution.
const decodeViaElement = (url, { width, height }, d, timeoutMs) => new Promise((resolve, reject) => {
  const img = d.makeImage();
  let settled = false;
  let timer = null;
  // The timer must die with the decode, or it keeps the img alive for the full 10 s.
  const finish = (fn, v) => {
    if (settled) return;
    settled = true;
    if (timer != null && d.clearTimer) d.clearTimer(timer);
    fn(v);
  };
  img.addEventListener('load', () => finish(resolve, img));
  img.addEventListener('error', () => finish(reject, new Error(DECODE_ERROR)));
  try {
    img.width = width;
    img.height = height;
    img.decoding = 'async';
  } catch { /* stubbed or locked element — the src assignment below is what matters */ }
  timer = d.timer(() => finish(reject, new Error('image decode timed out')), timeoutMs || DECODE_TIMEOUT_MS);
  img.src = url;
});

// Bitmap path first (never for SVG), element path as the fallback; a `use` failure on the
// bitmap path also falls through, since the element decoder handles more.
const decode = async ({ dataUrl = '', blob = null }, { d, timeoutMs, elementSize, noSource }, use) => {
  // Both decoders reach the network for an http(s) source (fetch, then <img src>), so
  // the SSRF guard sits here, ahead of either. data:/blob: pass (urlGuard.js).
  if (dataUrl && !isAllowedImageUrl(dataUrl)) throw new Error(BLOCKED_URL);
  const type = (blob && blob.type) || mediaTypeOf(dataUrl);
  const vector = isSvgType(type) || (!type && isSvgUrl(dataUrl));

  if (!vector && d.createBitmap) {
    try {
      const bytes = blob || (dataUrl ? await d.toBlob(dataUrl) : null);
      if (bytes) {
        const bmp = await d.createBitmap(bytes);
        try {
          return await use({ vector, src: bmp, width: bmp.width, height: bmp.height });
        } finally {
          try { bmp.close?.(); } catch { /* not closeable */ }
        }
      }
    } catch { /* fall through to the element path */ }
  }

  let url = dataUrl;
  let revoke = '';
  if (!url && blob) { url = d.objectUrl(blob); revoke = url; }
  if (!url) throw new Error(noSource);
  try {
    const el = await decodeViaElement(url, elementSize(vector), d, timeoutMs);
    return await use({ vector, src: el, width: el.naturalWidth || 0, height: el.naturalHeight || 0 });
  } finally {
    if (revoke) { try { d.revokeUrl(revoke); } catch { /* noop */ } }
  }
};

// Dims 0 = unknown.
export const rasterizeToPngDataUrl = async ({ dataUrl = '', blob = null, width = 0, height = 0 } = {}, opts = {}) => {
  const d = deps(opts.deps);
  const maxEdge = opts.maxEdge || DEFAULT_MAX_EDGE;
  const fallbackEdge = opts.fallbackEdge || DEFAULT_RASTER_EDGE;
  // Only vector sources upscale (it costs nothing for vector art).
  const size = (vector, natural = {}) => rasterSize({
    width, height, ...natural, maxEdge, fallbackEdge, minEdge: vector ? fallbackEdge : 0,
  });
  return decode({ dataUrl, blob }, {
    d, timeoutMs: opts.timeoutMs, elementSize: size, noSource: 'no image source to rasterise',
  }, ({ vector, src, width: nw, height: nh }) =>
    drawToPng(src, size(vector, { naturalWidth: nw, naturalHeight: nh }), d));
};

// Intrinsic pixel size via the same two-step decode (so an SVG measures too).
export const decodeSize = async ({ dataUrl = '', blob = null } = {}, opts = {}) => {
  const d = deps(opts.deps);
  return decode({ dataUrl, blob }, {
    d,
    timeoutMs: opts.timeoutMs,
    elementSize: () => rasterSize({ fallbackEdge: DEFAULT_RASTER_EDGE }),
    noSource: 'no image source to measure',
  }, ({ width, height }) => ({ width, height }));
};
