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
    luma[i] = (2126 * data[p] + 7152 * data[p + 1] + 722 * data[p + 2]) / 10_000;   // the store truncates
  }

  // Edge-replicated (clamped) luma lookup, for the first and last column only — 1×1/1×N images
  // work via the clamping. The interior runs branch-free over three row offsets (sobelRows).
  const l = (x, y) => {
    if (x < 0) x = 0; else if (x >= width) x = width - 1;
    if (y < 0) y = 0; else if (y >= height) y = height - 1;
    return luma[y * width + x];
  };
  const write = (p, gx, gy) => {
    const v = 255 - Math.min(255, Math.abs(gx) + Math.abs(gy));   // dark edges on white
    data[p] = v;
    data[p + 1] = v;
    data[p + 2] = v;
  };
  const edge = (x, y) => write((y * width + x) * 4,
    (l(x + 1, y - 1) + 2 * l(x + 1, y) + l(x + 1, y + 1)) - (l(x - 1, y - 1) + 2 * l(x - 1, y) + l(x - 1, y + 1)),
    (l(x - 1, y + 1) + 2 * l(x, y + 1) + l(x + 1, y + 1)) - (l(x - 1, y - 1) + 2 * l(x, y - 1) + l(x + 1, y - 1)));

  for (let y = 0; y < height; y++) {
    const up = Math.max(y - 1, 0) * width;
    const mid = y * width;
    const dn = Math.min(y + 1, height - 1) * width;
    edge(0, y);
    for (let x = 1; x + 1 < width; x++) {
      const a = luma[up + x - 1], b = luma[up + x], c = luma[up + x + 1];
      const d = luma[mid + x - 1], f = luma[mid + x + 1];
      const g = luma[dn + x - 1], h = luma[dn + x], i = luma[dn + x + 1];
      write((mid + x) * 4, (c + 2 * f + i) - (a + 2 * d + g), (g + 2 * h + i) - (a + 2 * b + c));
    }
    if (width > 1) edge(width - 1, y);
  }
};
