// The RGBA pass of the pixel filters, shared by filterCanvas.js and the image worker so both
// run one implementation over the wasm core when it is loaded, else its JS twin.

export declare const FILTER_RECIPE: Readonly<{ CONTOUR: 'contour'; CUSTOM: 'custom'; TINT: 'tint' }>;
export type FilterRecipe = typeof FILTER_RECIPE[keyof typeof FILTER_RECIPE];

/** Duotone over pixels already grayscaled: dark → (r,g,b), light → white. In place. */
export declare const tintRGBA: (data: Uint8ClampedArray | Uint8Array, r: number, g: number, b: number) => void;

/** In place; false when CUSTOM needs the wasm op and `op` has none. */
export declare const filterPixels: (
  recipe: FilterRecipe, data: Uint8ClampedArray | Uint8Array, width: number, height: number,
  rgb: { r: number; g: number; b: number },
  op: (name: 'applyContourRGBA' | 'applyFilterRGBA') => ((...args: never[]) => void) | null,
) => boolean;
