// Video-frame capture for `stencil.load(videoUrl)` and the open-image modal. Browser-only.
import { scaledDataUrl } from '../../utils.js';
import MEDIA_TYPES from '../../config/mediaTypes.json' with { type: 'json' };

// From config/mediaTypes.json (`surfaces.browser`); the two lists deliberately differ from
// each other and from every other surface — see the asset's `drift` note.
const { videoFile, videoUrl } = MEDIA_TYPES.surfaces.browser;
const VIDEO_FILE_RE = new RegExp(`\\.(${videoFile.join('|')})$`, 'i');
const VIDEO_URL_RE = new RegExp(`\\.(${videoUrl.join('|')})(\\?|#|$)`, 'i');

export function isVideoFile(file) {
  if (!file) return false;
  if (typeof file.type === 'string' && file.type.startsWith('video/')) return true;
  return VIDEO_FILE_RE.test(file.name || '');
}

// By path extension (a URL carries no MIME up front), tolerating a ?query / #hash.
export function isVideoUrl(url) {
  return typeof url === 'string' && VIDEO_URL_RE.test(url.trim());
}

const VIDEO_STEP_TIMEOUT_MS = 8000;
const FRAME_MAX_EDGE = 1920;
const FRAME_JPEG_QUALITY = 0.92;

// The CURRENT frame as a JPEG data URL. Throws on a tainted/cross-origin canvas.
const captureCurrentFrame = (v) =>
  scaledDataUrl(v, v.videoWidth, v.videoHeight, FRAME_MAX_EDGE, 'image/jpeg', FRAME_JPEG_QUALITY);

// Decode `srcUrl` ONCE, seek sequentially through `timesFor(duration)`, capture each frame.
// The per-step timeout is re-armed on every load/seek. Revokes `srcUrl` when done.
function captureFramesAt(srcUrl, timesFor) {
  return new Promise((resolve, reject) => {
    const v = document.createElement('video');
    v.muted = true; v.preload = 'auto';
    // Ask for CORS before the src: this element's frames are READ BACK, so without the
    // ask a permitting host's video still taints the canvas and the capture throws.
    if (!/^(blob|data):/i.test(srcUrl)) v.crossOrigin = 'anonymous';
    v.src = srcUrl;
    const out = [];
    let times = [];
    let done = false;
    let timer = 0;
    const finish = (err) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      URL.revokeObjectURL(srcUrl);
      if (err) reject(err); else resolve(out);
    };
    const arm = () => {
      clearTimeout(timer);
      timer = setTimeout(() => finish(new Error('video frame timeout')), VIDEO_STEP_TIMEOUT_MS);
    };
    const seekNext = () => {
      arm();
      try { v.currentTime = times[out.length]; }
      catch { finish(new Error('video seek failed')); }
    };
    v.addEventListener('loadeddata', () => { times = timesFor(v.duration); seekNext(); });
    v.addEventListener('seeked', () => {
      if (done) return;
      try { out.push(captureCurrentFrame(v)); }
      catch { return finish(new Error('video frame capture failed (tainted/cross-origin?)')); }
      if (out.length < times.length) seekNext(); else finish();
    });
    v.addEventListener('error', () => finish(new Error('video failed to load')));
    arm();
  });
}

// The frame at `timeSec` as a JPEG data URL. Revokes `srcUrl` when done.
export async function videoFrameDataUrl(srcUrl, timeSec) {
  const [dataUrl] = await captureFramesAt(srcUrl, (duration) =>
    [Math.min(Number(timeSec) || 0, Math.max(0, (duration || 0) - 0.01))]);
  return dataUrl;
}

// The frame at `timeSec` as an image File (a JPEG) with the file's base name.
export async function videoFileToImageFile(file, timeSec = 0) {
  const dataUrl = await videoFrameDataUrl(URL.createObjectURL(file), Number(timeSec) || 0);
  const blob = await (await fetch(dataUrl)).blob();
  const base = (file.name || 'frame').replace(/\.[^.]+$/, '');
  return new File([blob], `${base}.jpg`, { type: 'image/jpeg' });
}

// `count` evenly-spaced frames (first included). The chat panel attaches these as images —
// videos themselves are never sent to the LLM (llm-contract.md §7).
export function videoFrameSamples(file, count = 4) {
  const n = Math.max(1, Math.round(count) || 1);
  return captureFramesAt(URL.createObjectURL(file), (rawDuration) => {
    const duration = Number.isFinite(rawDuration) ? rawDuration : 0;
    return Array.from({ length: n }, (_, i) => Math.min((duration * i) / n, Math.max(0, duration - 0.01)));
  });
}

// The browser cannot read a video's true frame rate, so index / 30 s approximates the
// CLI's exact frame pick (ffmpeg select=eq(n, index)).
export const FRAME_INDEX_FPS = 30;
export function videoFrameByIndex(file, index) {
  return videoFrameDataUrl(URL.createObjectURL(file), Math.max(0, Number(index) || 0) / FRAME_INDEX_FPS);
}
