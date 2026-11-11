// A project that is not open, drawn off-screen as the editor draws it: its original turned, mirrored
// and cropped (image/model.js rebuildCroppedImage), then its filter or tint (filterCanvas.js), then —
// for "current" — its resting lines (draw/restingPaint.js). The decoded bitmap is closed on every path.
import { ImageFilterCanvas } from '../../core/image/filterCanvas.js';
import { paintRestingLines, restingPainter } from '../../core/draw/restingPaint.js';
import { sanitizeLines, normalizeCropRect } from '../../core/layout.js';

const DEFAULT_TINT = '#7c3aed';

const canvasOf = (w, h) => {
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  return c;
};
// The filter's copy may be an OffscreenCanvas (filterCanvas.js makeCanvas), which encodes through convertToBlob.
export const pngOf = (canvas) => (typeof canvas.convertToBlob === 'function'
  ? canvas.convertToBlob({ type: 'image/png' })
  : new Promise((res, rej) => canvas.toBlob((b) => (b ? res(b) : rej(new Error('Image encode failed'))), 'image/png')));

// The turn and the crop offset as one transform; no crop is the whole turned picture.
export const orientedCrop = (img, layout = {}) => {
  const q = (((layout.rotationQuarters | 0) % 4) + 4) % 4;
  const rw = q % 2 ? img.height : img.width;
  const rh = q % 2 ? img.width : img.height;
  const crop = normalizeCropRect(layout.cropRect);
  const r = crop && crop.width > 0 && crop.height > 0 ? crop : { x: 0, y: 0, width: rw, height: rh };
  const c = canvasOf(Math.round(r.width), Math.round(r.height));
  const ctx = c.getContext('2d');
  ctx.translate(rw / 2 - r.x, rh / 2 - r.y);
  ctx.rotate(q * Math.PI / 2);
  if (layout.mirrored) ctx.scale(-1, 1);
  ctx.drawImage(img, -img.width / 2, -img.height / 2);
  return c;
};

// `variant`: 'current' (filter and lines), 'tint' (filter only), 'original' (neither). `look` is the
// app's own showLines / showPoints / pointSize, as the project would show once opened.
export const renderProject = async (blob, layout = {}, variant = 'current', look = {}) => {
  const bmp = await createImageBitmap(blob);
  try {
    const base = orientedCrop(bmp, layout);
    if (variant === 'original') return await pngOf(base);
    const filter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
    const tint = filter === 'custom' ? (layout.filterColor || DEFAULT_TINT) : null;
    const filtered = new ImageFilterCanvas().canvasFor(base, filter, tint) || base;
    if (variant === 'tint') return await pngOf(filtered);
    const out = canvasOf(base.width, base.height);
    const ctx = out.getContext('2d');
    ctx.drawImage(filtered, 0, 0);
    const opts = { showLines: look.showLines !== false, showPoints: look.showPoints !== false, pointSize: look.pointSize || 4 };
    paintRestingLines(restingPainter(ctx, opts), sanitizeLines(layout.lines), opts);
    return await pngOf(out);
  } finally {
    bmp.close();
  }
};
