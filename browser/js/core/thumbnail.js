// ── The projects-list thumbnail ─────────────────────────────────
// Extracted from storage.js. It is ALSO the hover-zoom source (projectsModal magnifies it
// ~1.67x), so it is sized for that, not for the 56px row — and it lives in the
// localStorage registry, so it stays a JPEG and stays modest.
const THUMB_MAX_PX = 480;
const THUMB_QUALITY = 0.85;

export const makeThumbnail = (app) => {
  if (!app.image) return null;
  try {
    // Render the EDITED result (filter + lines), not the raw original, so the
    // projects list previews match what the user actually drew/filtered.
    const src = app.renderResultCanvas();
    // Sized for the LARGEST consumer, not the row: the projects list magnifies this
    // same bitmap on hover (projectsModal PREVIEW_ZOOM).
    const max = THUMB_MAX_PX;
    const iw = src.width;
    const ih = src.height;
    const scale = Math.min(1, max / Math.max(iw, ih));
    const w = Math.max(1, Math.round(iw * scale));
    const h = Math.max(1, Math.round(ih * scale));
    // A single drawImage from (say) 2657px down to 480 is an ~5x reduction, which the
    // bilinear filter can't sample properly — it aliases. Halving repeatedly until
    // within 2x of the target keeps every source pixel contributing.
    let cur = src;
    while (cur.width > w * 2 && cur.height > h * 2) {
      const half = document.createElement('canvas');
      half.width = Math.max(w, Math.round(cur.width / 2));
      half.height = Math.max(h, Math.round(cur.height / 2));
      const hc = half.getContext('2d');
      hc.imageSmoothingEnabled = true;
      hc.imageSmoothingQuality = 'high';
      hc.drawImage(cur, 0, 0, half.width, half.height);
      cur = half;
    }
    const offscreen = document.createElement('canvas');
    offscreen.width = w;
    offscreen.height = h;
    const ctx = offscreen.getContext('2d');
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(cur, 0, 0, w, h);
    // 0.6 put visible JPEG blocking on faces at this size; 0.85 is where that stops
    // being obvious without the payload growing unreasonably (thumbs live in the
    // localStorage registry, so they are still deliberately modest).
    return offscreen.toDataURL('image/jpeg', THUMB_QUALITY);
  } catch {
    return null;
  }
};

