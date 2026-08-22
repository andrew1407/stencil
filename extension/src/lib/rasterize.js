// ── Rasterising ANY scanned image source to PNG bytes ───────────────────────
// The LLM contract (llm-contract.md §7) accepts image/png|jpeg|webp|gif only —
// an SVG must be RASTERISED, never sent as-is. It also has to be: Chrome's
// createImageBitmap() flatly refuses an `image/svg+xml` Blob ("The source image could
// not be decoded"), so every attach path funnels through here, decoding in two steps:
//   1. createImageBitmap on the bytes — fast and exact, skipped for SVG;
//   2. an <img> element at an EXPLICIT pixel size + canvas.drawImage — the same
//      decoder the popup thumbnails use. This handles SVG (including one with no
//      intrinsic width/height, which has no natural size to scale from), and anything
//      else the bitmap decoder chokes on.
// Sources are always fed in as `data:` URLs (lib/stencil.js fetchAsDataUrl pulls the
// bytes through the extension's host permissions, so cross-origin/opaque responses and
// hotlink protection are already handled upstream) or as local File/Blob objects —
// either way the canvas is never tainted.
//
// Every DOM seam (bitmap decoder, image element, canvas, blob/object URLs) is INJECTED,
// so `node --test` drives the whole module with stubs.

// Long edge a decoded image is fitted into by default (contract §7 downscale).
export const DEFAULT_MAX_EDGE = 1568;
// Long edge used when the source has NO usable pixel size — the common case for an
// SVG declared with only a viewBox. Also the minimum a VECTOR source is rasterised at:
// upscaling costs nothing for vector art and a 16×16 favicon-sized render is useless
// for vision analysis.
export const DEFAULT_RASTER_EDGE = 512;
// How long to wait for the <img> decode before giving up (a stuck load must not hang
// the chat's send loop).
export const DECODE_TIMEOUT_MS = 10000;

export const DECODE_ERROR = 'the image could not be decoded (unsupported or blocked source)';

// Is this media type / URL an SVG (the type that must never reach createImageBitmap
// and must never be sent to the model unrasterised)?
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

/**
 * The pixel size to rasterise at. Pure — the whole size policy in one place:
 *   1. the caller's KNOWN dims (the scan entry's w/h) fitted to `maxEdge`;
 *   2. else the decoded source's own intrinsic size, fitted the same way;
 *   3. else a `fallbackEdge` square (an SVG with only a viewBox has neither).
 * `minEdge` (vector sources) then scales the result UP to that long edge.
 */
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

// The real browser seams. Built lazily (inside a function) so importing this module
// under `node --test`, where none of these globals exist, is harmless.
const domDeps = () => ({
  createBitmap: typeof createImageBitmap === 'function' ? (blob) => createImageBitmap(blob) : null,
  makeImage: () => document.createElement('img'),
  makeCanvas: () => document.createElement('canvas'),
  toBlob: async (url) => (await fetch(url)).blob(),
  objectUrl: (blob) => URL.createObjectURL(blob),
  revokeUrl: (url) => URL.revokeObjectURL(url),
  timer: (fn, ms) => setTimeout(fn, ms),
  clearTimer: (id) => clearTimeout(id),
});

const deps = (extra) => ({ ...domDeps(), ...(extra || {}) });

// Draw a decoded bitmap/element into a canvas of exactly `size` and export PNG.
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

// Decode `url` with an <img>. The explicit width/height is load-bearing: an SVG with
// no intrinsic size has nothing to lay out against, and Chrome renders it at the
// element's box — so the box IS the rasterisation resolution.
const decodeViaElement = (url, { width, height }, d, timeoutMs) => new Promise((resolve, reject) => {
  const img = d.makeImage();
  let settled = false;
  let timer = null;
  // The timeout timer must die with the decode: left running, it keeps the img (and
  // this closure) alive for the full 10s after every successful attach.
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

// The shared two-step decode skeleton. Detects a vector source, tries the bitmap
// path (never for SVG — Chrome rejects it outright), and falls back to the element
// path, managing the bitmap's and any object URL's lifetime. The decoded source is
// handed to `use({ vector, src, width, height })`; a `use` failure on the bitmap
// path also falls through to the element decoder (it decodes more than the bitmap
// API). `elementSize(vector)` is the <img> box — for an SVG with no intrinsic size,
// the box IS the rasterisation resolution.
const decode = async ({ dataUrl = '', blob = null }, { d, timeoutMs, elementSize, noSource }, use) => {
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

/**
 * Rasterise any image source to a PNG data URL, downscaled to `maxEdge`.
 * @param {object} source - `{ dataUrl?, blob?, width?, height? }`; `width`/`height`
 *   are the scan entry's known pixel dims (0 = unknown).
 * @param {object} [opts] - `{ maxEdge, fallbackEdge, timeoutMs, deps }`.
 * @returns {Promise<string>} `data:image/png;base64,…`
 */
export const rasterizeToPngDataUrl = async ({ dataUrl = '', blob = null, width = 0, height = 0 } = {}, opts = {}) => {
  const d = deps(opts.deps);
  const maxEdge = opts.maxEdge || DEFAULT_MAX_EDGE;
  const fallbackEdge = opts.fallbackEdge || DEFAULT_RASTER_EDGE;
  // `minEdge` upscales only VECTOR sources (upscaling costs nothing for vector art).
  const size = (vector, natural = {}) => rasterSize({
    width, height, ...natural, maxEdge, fallbackEdge, minEdge: vector ? fallbackEdge : 0,
  });
  return decode({ dataUrl, blob }, {
    d, timeoutMs: opts.timeoutMs, elementSize: size, noSource: 'no image source to rasterise',
  }, ({ vector, src, width: nw, height: nh }) =>
    drawToPng(src, size(vector, { naturalWidth: nw, naturalHeight: nh }), d));
};

/**
 * The intrinsic pixel size of an image source, using the same two-step decode
 * (so an SVG measures too). Throws when nothing can decode it.
 * @param {object} source - `{ dataUrl?, blob? }`
 * @returns {Promise<{width: number, height: number}>}
 */
export const decodeSize = async ({ dataUrl = '', blob = null } = {}, opts = {}) => {
  const d = deps(opts.deps);
  return decode({ dataUrl, blob }, {
    d,
    timeoutMs: opts.timeoutMs,
    elementSize: () => rasterSize({ fallbackEdge: DEFAULT_RASTER_EDGE }),
    noSource: 'no image source to measure',
  }, ({ width, height }) => ({ width, height }));
};
