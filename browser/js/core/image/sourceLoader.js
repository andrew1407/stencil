// The bytes half of the Open-Image dialog: fetch a URL, turn a video into a captured frame.
import { guardedFetch } from '../../net/fetchGuard.js';
import { isVideoFile, videoFileToImageFile } from '../export/videoFrame.js';

// Same-origin / data: / CORS-enabled; a canvas readback would taint without CORS.
export const fetchUrlToFile = async (url) => {
  const resp = await guardedFetch(url);
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  const blob = await resp.blob();
  const ext = (blob.type.split('/')[1] || url.split(/[?#]/)[0].split('.').pop() || 'png').slice(0, 5);
  const name = (url.split(/[?#]/)[0].split('/').pop() || 'image').replace(/\.[a-z0-9]+$/i, '') + '.' + ext;
  return new File([blob], name, { type: blob.type || 'image/png' });
};

export const toFrameIfVideo = async (file, frameIndex = 0) => {
  if (!isVideoFile(file)) return file;
  return await videoFileToImageFile(file, frameIndex);
};
