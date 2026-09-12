// ── Pixel work shared by the image worker and its inline fallback ──
// Pure over a canvas factory, so the SAME drawImage sequence runs on an OffscreenCanvas in
// the worker and on a document canvas on the main thread — one encoder, one set of pixels.

// ≤ maxEdge px on the long edge, never upscaled, at least 1px a side.
export const fitSize = (width, height, maxEdge) => {
  const k = Math.min(1, maxEdge / Math.max(width, height));
  return { width: Math.max(1, Math.round(width * k)), height: Math.max(1, Math.round(height * k)) };
};

// Draw `source` (sw×sh) onto a fresh width×height canvas. `halve` steps down by 2× until
// within 2× of the target with high-quality smoothing — one draw from 2657px to 480 is
// a ~5x reduction the bilinear filter cannot sample, so it aliases; halving keeps every
// source pixel contributing. Off, it is the single plain draw of a chat attachment.
export const paintScaled = (makeCanvas, source, sw, sh, { width, height, halve = false }) => {
  let cur = source, cw = sw, ch = sh;
  const smooth = (ctx) => { ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high'; };
  while (halve && cw > width * 2 && ch > height * 2) {
    const half = makeCanvas(Math.max(width, Math.round(cw / 2)), Math.max(height, Math.round(ch / 2)));
    const hc = half.getContext('2d');
    smooth(hc);
    hc.drawImage(cur, 0, 0, half.width, half.height);
    cur = half; cw = half.width; ch = half.height;
  }
  const out = makeCanvas(width, height);
  const ctx = out.getContext('2d');
  if (halve) smooth(ctx);
  ctx.drawImage(cur, 0, 0, width, height);
  return out;
};

// Run `contour` (the core Sobel pass) over `imageData` in place and paint it onto a fresh canvas.
export const contourCanvas = (makeCanvas, imageData, contour) => {
  const { width, height } = imageData;
  contour(imageData.data, width, height);
  const out = makeCanvas(width, height);
  out.getContext('2d').putImageData(imageData, 0, 0);
  return out;
};
