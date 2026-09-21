// Contour (Sobel edge-detection) filter — JS reference + fallback.
// Port of core/raster/imageFilter.cpp applyContourRGBA; must stay byte-identical to that
// integer math: L = trunc((2126*r + 7152*g + 722*b) / 10000) over the ORIGINAL pixels,
// Sobel gx/gy with edge-replicated neighbours, mag = min(255, |gx| + |gy|),
// r = g = b = 255 - mag with alpha preserved. Mutates RGBA8 `data` in place; no DOM.

export const applyContourRGBA = (data, width, height) => {
  if (!data || width <= 0 || height <= 0) return;

  // Luma from the original pixels, truncating like the C++. Provably 0..255
  // ((2126+7152+722)·255/10000 = 255 exactly), so a byte plane suffices.
  const count = width * height;
  const luma = new Uint8Array(count);
  for (let i = 0; i < count; i++) {
    const p = i * 4;
    luma[i] = Math.trunc((2126 * data[p] + 7152 * data[p + 1] + 722 * data[p + 2]) / 10_000);
  }

  // Edge-replicated (clamped) luma lookup — 1×1/1×N images work via the clamping
  // (gx/gy collapse to 0 where every neighbor is the same pixel).
  const l = (x, y) => {
    if (x < 0) x = 0; else if (x >= width) x = width - 1;
    if (y < 0) y = 0; else if (y >= height) y = height - 1;
    return luma[y * width + x];
  };

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const gx = (l(x + 1, y - 1) + 2 * l(x + 1, y) + l(x + 1, y + 1)) -
                 (l(x - 1, y - 1) + 2 * l(x - 1, y) + l(x - 1, y + 1));
      const gy = (l(x - 1, y + 1) + 2 * l(x, y + 1) + l(x + 1, y + 1)) -
                 (l(x - 1, y - 1) + 2 * l(x, y - 1) + l(x + 1, y - 1));
      const mag = Math.min(255, Math.abs(gx) + Math.abs(gy));
      const p = (y * width + x) * 4;
      const v = 255 - mag;   // dark edges on white
      data[p] = v;
      data[p + 1] = v;
      data[p + 2] = v;
    }
  }
};
