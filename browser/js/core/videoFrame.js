// Shared video-frame capture: decode a video and grab a still frame to an image.
// Used by both `stencil.load(videoUrl)` and the open-image modal. Browser-only.
import { scaledDataUrl } from '../utils.js';

// Is this File a video (by MIME, falling back to a common video extension)? Pure.
export function isVideoFile(file) {
  if (!file) return false;
  if (typeof file.type === 'string' && file.type.startsWith('video/')) return true;
  return /\.(mp4|webm|ogg|ogv|mov|m4v|mkv|avi)$/i.test(file.name || '');
}

// Does this URL point at a video (by its path extension, tolerating a ?query / #hash)?
// Pure — the open-image dialog uses it to decide whether a URL source needs a frame
// picker (a URL carries no MIME up front, so extension is the only signal we have).
export function isVideoUrl(url) {
  return typeof url === 'string'
    && /\.(mp4|mov|webm|mkv|avi|m4v|ogv|mpe?g)(\?|#|$)/i.test(url.trim());
}

const VIDEO_STEP_TIMEOUT_MS = 8000;   // per load/seek step, matching the original single-frame budget
const FRAME_MAX_EDGE = 1920;
const FRAME_JPEG_QUALITY = 0.92;

// Draw the video's CURRENT frame to a JPEG data URL (≤ FRAME_MAX_EDGE on the long
// edge). Throws on a tainted/cross-origin canvas.
const captureCurrentFrame = (v) =>
  scaledDataUrl(v, v.videoWidth, v.videoHeight, FRAME_MAX_EDGE, 'image/jpeg', FRAME_JPEG_QUALITY);

// The one <video> lifecycle every capture path shares: decode `srcUrl` ONCE, seek
// sequentially through the times `timesFor(duration)` returns, and capture each
// frame as a JPEG data URL. The per-step timeout is re-armed on every load/seek so
// the total budget scales with the frame count. Revokes `srcUrl` when done
// (success or failure). Rejects on load/seek/taint/timeout.
function captureFramesAt(srcUrl, timesFor) {
  return new Promise((resolve, reject) => {
    const v = document.createElement('video');
    v.muted = true; v.preload = 'auto'; v.src = srcUrl;
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

// Decode a video blob/object URL and capture the frame at `timeSec` to a JPEG data
// URL. Revokes `srcUrl` when done (success or failure). Rejects on load/seek/taint.
export async function videoFrameDataUrl(srcUrl, timeSec) {
  const [dataUrl] = await captureFramesAt(srcUrl, (duration) =>
    [Math.min(Number(timeSec) || 0, Math.max(0, (duration || 0) - 0.01))]);
  return dataUrl;
}

// Capture a frame from a local video File into an image File (a JPEG), reusing the
// file's base name. `timeSec` selects the frame (default the first frame).
export async function videoFileToImageFile(file, timeSec = 0) {
  const dataUrl = await videoFrameDataUrl(URL.createObjectURL(file), Number(timeSec) || 0);
  const blob = await (await fetch(dataUrl)).blob();
  const base = (file.name || 'frame').replace(/\.[^.]+$/, '');
  return new File([blob], `${base}.jpg`, { type: 'image/jpeg' });
}

// Capture `count` evenly-spaced frames of a video File as JPEG data URLs (first frame
// included). The chat panel attaches these as images — videos themselves are never
// sent to the LLM (llm-contract.md §7). Decodes the video ONCE and seeks
// sequentially (not one full reload per frame).
export function videoFrameSamples(file, count = 4) {
  const n = Math.max(1, Math.round(count) || 1);
  return captureFramesAt(URL.createObjectURL(file), (rawDuration) => {
    const duration = Number.isFinite(rawDuration) ? rawDuration : 0;
    return Array.from({ length: n }, (_, i) => Math.min((duration * i) / n, Math.max(0, duration - 0.01)));
  });
}

// Nominal fps for mapping a frame INDEX onto a seek time: the browser cannot read a
// video's true frame rate, so index / 30 s approximates the CLI's exact frame pick
// (ffmpeg select=eq(n, index)). Used by the assistant's "frame" op.
const FRAME_INDEX_FPS = 30;
export function videoFrameByIndex(file, index) {
  return videoFrameDataUrl(URL.createObjectURL(file), Math.max(0, Number(index) || 0) / FRAME_INDEX_FPS);
}
