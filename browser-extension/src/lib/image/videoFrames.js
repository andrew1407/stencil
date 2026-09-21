// Videos are never sent to the model (llm-contract.md §7): they are sampled into evenly
// spaced JPEG frames. Browser twin: browser/js/core/export/videoFrame.js captureFramesAt; this
// copy downscales to the contract's image edge instead of the editor's 1920.
import { fitSize } from './rasterize.js';
import { MAX_IMAGE_EDGE } from '../../llm/chatController.js';

const VIDEO_FRAME_COUNT = 4;
const VIDEO_TIMEOUT_MS = 8000;
const VIDEO_JPEG_QUALITY = 0.92;
const VIDEO_DECODE_ERROR = 'this video can’t be decoded here — its codec is not supported';


// Never below 1×1.
const fitCanvas = (w, h) => {
  const s = fitSize(w, h, MAX_IMAGE_EDGE);
  const c = document.createElement('canvas');
  c.width = s.width || 1;
  c.height = s.height || 1;
  return c;
};


// Bounded: a stuck decode must not hang the chat's send loop.
const nextVideoEvent = (v, type, timeoutMsg) => new Promise((resolve, reject) => {
  let done = false;
  const fin = (fn, x) => {
    if (done) return;
    done = true;
    clearTimeout(timer);
    v.removeEventListener(type, ok);
    v.removeEventListener('error', err);
    fn(x);
  };
  const ok = () => fin(resolve);
  const err = () => fin(reject, new Error(VIDEO_DECODE_ERROR));
  const timer = setTimeout(() => fin(reject, new Error(timeoutMsg)), VIDEO_TIMEOUT_MS);
  v.addEventListener(type, ok);
  v.addEventListener('error', err);
});

// One <video> decodes the whole pass, seeked sequentially — never re-decoding per frame.
export const sampleVideoFrames = async (blob, count = VIDEO_FRAME_COUNT) => {
  const url = URL.createObjectURL(blob);
  const v = document.createElement('video');
  v.muted = true; v.preload = 'auto';
  try {
    const loaded = nextVideoEvent(v, 'loadeddata', 'video metadata timeout');
    v.src = url;
    await loaded;
    const duration = Number.isFinite(v.duration) ? v.duration : 0;
    const out = [];
    for (let i = 0; i < count; i++) {
      const seeked = nextVideoEvent(v, 'seeked', 'video frame timeout');
      try { v.currentTime = Math.min((duration * i) / count, Math.max(0, duration - 0.01)); }
      catch { throw new Error('video seek failed'); }
      await seeked;
      try {
        const c = fitCanvas(v.videoWidth, v.videoHeight);
        c.getContext('2d').drawImage(v, 0, 0, c.width, c.height);
        out.push(c.toDataURL('image/jpeg', VIDEO_JPEG_QUALITY));
      } catch { throw new Error('video frame capture failed'); }
    }
    return out;
  } finally {
    v.removeAttribute('src');   // release the decoder before the URL goes away
    URL.revokeObjectURL(url);
  }
};
