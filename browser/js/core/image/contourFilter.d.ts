// Contour (Sobel edge-detection) filter — the JS reference and fallback for
// core/raster/imageFilter.cpp applyContourRGBA, integer-only so both agree byte for byte.

/** Replace the RGB of an interleaved RGBA8 buffer with 255 − Sobel magnitude, in place. */
export declare const applyContourRGBA: (data: Uint8ClampedArray | Uint8Array, width: number, height: number) => void;
