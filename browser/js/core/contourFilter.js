// ── Contour (Sobel edge-detection) filter — JS reference + fallback ─────────
// Port of core/color/imageFilter.cpp applyContourRGBA. The renderer prefers the
// wasm build of that function; this body is the fallback and must stay
// byte-identical to the C++ (the pinned integer-only math):
//   1. luma plane L = trunc((2126*r + 7152*g + 722*b) / 10000), computed from the
//      ORIGINAL pixels before any output is written
//   2. Sobel gx/gy with edge-replicated (clamped) neighbor coordinates
//   3. mag = min(255, |gx| + |gy|)
//   4. r = g = b = 255 - mag (dark edges on white); the alpha byte is preserved
// Mutates `data` (an interleaved RGBA8 Uint8ClampedArray/Uint8Array of
// width × height pixels) in place. Pure — no DOM, importable under Node.

export const applyContourRGBA = (data, width, height) => {
  if (!data || width <= 0 || height <= 0) return;

  // Luma plane from the original pixels (truncating division, like the C++).
  // Every value is provably 0..255 ((2126+7152+722)·255/10000 = 255 exactly), so a
  // byte plane suffices — the Sobel sums below accumulate in plain Numbers.
  const count = width * height;
  const luma = new Uint8Array(count);
  for (let i = 0; i < count; i++) {
    const p = i * 4;
    luma[i] = Math.trunc((2126 * data[p] + 7152 * data[p + 1] + 722 * data[p + 2]) / 10000);
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
      // data[p + 3] (alpha) is left unchanged.
    }
  }
};
