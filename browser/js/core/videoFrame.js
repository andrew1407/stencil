// Shared video-frame capture: decode a video and grab a still frame to an image.
// Used by both `stencil.load(videoUrl)` and the open-image modal. Browser-only.

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

// Decode a video blob/object URL and capture the frame at `timeSec` to a JPEG data
// URL. Revokes `srcUrl` when done (success or failure). Rejects on load/seek/taint.
export function videoFrameDataUrl(srcUrl, timeSec) {
  return new Promise((resolve, reject) => {
    const v = document.createElement('video');
    v.muted = true; v.preload = 'auto'; v.src = srcUrl;
    let done = false;
    const fail = (msg) => { if (!done) { done = true; URL.revokeObjectURL(srcUrl); reject(new Error(msg)); } };
    v.addEventListener('loadeddata', () => {
      try { v.currentTime = Math.min(Number(timeSec) || 0, Math.max(0, (v.duration || 0) - 0.01)); }
      catch { fail('video seek failed'); }
    });
    v.addEventListener('seeked', () => {
      if (done) return;
      try {
        const k = Math.min(1, 1920 / Math.max(v.videoWidth, v.videoHeight));
        const c = document.createElement('canvas');
        c.width = Math.max(1, Math.round(v.videoWidth * k));
        c.height = Math.max(1, Math.round(v.videoHeight * k));
        c.getContext('2d').drawImage(v, 0, 0, c.width, c.height);
        done = true; URL.revokeObjectURL(srcUrl);
        resolve(c.toDataURL('image/jpeg', 0.92));
      } catch { fail('video frame capture failed (tainted/cross-origin?)'); }
    });
    v.addEventListener('error', () => fail('video failed to load'));
    setTimeout(() => fail('video frame timeout'), 8000);
  });
}

// Capture a frame from a local video File into an image File (a JPEG), reusing the
// file's base name. `timeSec` selects the frame (default the first frame).
export async function videoFileToImageFile(file, timeSec = 0) {
  const dataUrl = await videoFrameDataUrl(URL.createObjectURL(file), Number(timeSec) || 0);
  const blob = await (await fetch(dataUrl)).blob();
  const base = (file.name || 'frame').replace(/\.[^.]+$/, '');
  return new File([blob], `${base}.jpg`, { type: 'image/jpeg' });
}
