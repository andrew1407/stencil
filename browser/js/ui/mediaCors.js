// Cross-origin preview media. A host that sends CORS headers (most CDNs do) lets the
// canvas read the pixels back, which is what cropping and the arrival dust need; one that
// refuses REJECTS the load outright when we ask, so the ask is always followed by a plain
// retry. Browser-only: the desktop app fetches the bytes itself and never faces this.
export const loadMediaCors = (el, src, sameOrigin) => {
  el.dataset.corsTry = sameOrigin ? '' : '1';
  if (sameOrigin) el.removeAttribute('crossorigin');
  else el.crossOrigin = 'anonymous';
  el.src = src;
};

// The retry: drop the CORS ask and load the same source plainly. Returns false when the
// plain load is the one that just failed, so the caller can report a real error.
export const retryWithoutCors = (el, src) => {
  if (!el.dataset.corsTry) return false;
  el.dataset.corsTry = '';
  el.removeAttribute('crossorigin');
  el.src = src;
  return true;
};

// Whether the decoded media can be read back at all — the only honest answer, since it
// depends on the response's headers rather than on the URL's origin.
export const canReadPixels = (el) => {
  try {
    const c = document.createElement('canvas');
    c.width = c.height = 1;
    const ctx = c.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(el, 0, 0, 1, 1);
    ctx.getImageData(0, 0, 1, 1);
    return true;
  } catch {
    return false;
  }
};
