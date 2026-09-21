// The core's pixel ops over the wasm ABI: the RGBA filter table and the contour pass, both
// copying through the shared pixel scratch and writing the result back in place.
// Must match core/imageFilter.hpp.
const FILTER_MODE = { none: 0, bw: 1, sepia: 2, custom: 3, invert: 4, contour: 5 };

export const buildImageOps = (core, { pixelScratch }) => {
  const cFilter  = (mode, ptr, n, r, g, b) =>
    core.ccall('stencil_applyFilterRGBA', null, ['number', 'number', 'number', 'number', 'number', 'number'], [mode, ptr, n, r, g, b]);
  const cContour = (ptr, w, h) => core.ccall('stencil_applyContourRGBA', null, ['number', 'number', 'number'], [ptr, w, h]);

  return {
    applyFilterRGBA(mode, data, pixelCount, r, g, b) {
      const bytes = pixelCount * 4;
      const ptr = pixelScratch(bytes);
      core.HEAPU8.set(data, ptr);
      cFilter(FILTER_MODE[mode] ?? FILTER_MODE.custom, ptr, pixelCount, r, g, b);
      data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
    },

    // Contour needs the pixel neighborhood, so it crosses with width/height.
    applyContourRGBA(data, width, height) {
      const bytes = width * height * 4;
      const ptr = pixelScratch(bytes);
      core.HEAPU8.set(data, ptr);
      cContour(ptr, width, height);
      data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
    },
  };
};
