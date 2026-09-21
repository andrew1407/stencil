// Three routes to a still of the right-clicked <video>, tried in order by ctxActions: in-page
// canvas readback, an extension-side byte re-fetch, and a screenshot crop as a last resort.
import { blobToDataUrl } from '../lib/stencil.js';
import { isAllowedImageUrl } from '../lib/connection/urlGuard.js';

// An un-capped retina crop, as a data URL in the editor launch URL, overflows Chrome's limit.
const FRAME_MAX_SIDE = 1920;

// captureVisibleTab returns an extension-owned (never tainted) image.
export const captureFrameFromScreenshot = async (windowId, rect, dpr = 1) => {
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

// Direct draw (same-origin), then a fresh crossOrigin="anonymous" video at the same src/time
// (CORS CDNs). Returns { frame }, { src, t } (tainted; caller re-fetches), or null.
export const captureVideoFrameInTab = async (tabId, frameId, point) => {
  if (tabId == null) return null;
  const target = { tabId };
  if (frameId != null) target.frameIds = [frameId];
  try {
    const [res] = await chrome.scripting.executeScript({
      target,
      args: [point ? point.x : null, point ? point.y : null, FRAME_MAX_SIDE],
      func: async (px, py, maxSide) => {
        // Smallest <video> whose box contains the cursor — correct even under an overlay.
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
        // largest() is the no-point fallback (Chrome video context without a probe point).
        const v = px != null ? at(px, py) : largest();
        if (!v || !v.videoWidth || !v.videoHeight || v.readyState < 2) return null;
        // Paused at the very start → let the caller fall back to the poster, not a black frame.
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

// Current-frame capture for a tainted, non-CORS video: fetch bytes with host permissions,
// ship them into the page as a blob URL, draw the frame at the recorded time.
export const captureVideoFrameViaFetch = async (tabId, frameId, src, t, pageUrl = '') => {
  if (tabId == null || !src) return null;
  try {
    // `src` is page-derived — refuse private/internal targets (urlGuard.js).
    if (!isAllowedImageUrl(src, { allowSameHostAs: pageUrl })) return null;
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
