// ── Resolving an image SOURCE to a still-image File ─────────────────────────
// The bytes half of the Open-Image dialog: fetch a URL, and turn a video into a
// captured frame. No DOM — the dialog passes the chosen frame index in.
import { isVideoFile, videoFileToImageFile } from './videoFrame.js';

// Fetch a URL's bytes into a File (same-origin / data: / CORS-enabled), mirroring
// the old Links modal's honest fetch path (a canvas readback would taint without CORS).
export const fetchUrlToFile = async (url) => {
  const resp = await fetch(url);
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  const blob = await resp.blob();
  const ext = (blob.type.split('/')[1] || url.split(/[?#]/)[0].split('.').pop() || 'png').slice(0, 5);
  const name = (url.split(/[?#]/)[0].split('/').pop() || 'image').replace(/\.[a-z0-9]+$/i, '') + '.' + ext;
  return new File([blob], name, { type: blob.type || 'image/png' });
};

// A video source is converted to a captured still frame first; images pass through.
export const toFrameIfVideo = async (file, frameIndex = 0) => {
  if (!isVideoFile(file)) return file;
  return await videoFileToImageFile(file, frameIndex);
};
