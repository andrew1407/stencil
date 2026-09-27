import type { FilterRecipe } from './filterPixels.js';

/** The copy a filter paints: a canvas built here, or the ImageBitmap the image worker painted. */
export type FilterLayer = HTMLCanvasElement | OffscreenCanvas | ImageBitmap;

/** The input of a pixel filter's RGBA pass, read the way its painter reads it. */
export interface FilterInput {
  recipe: FilterRecipe;
  pixels: ImageData;
  rgb: { r: number; g: number; b: number };
}

/** 'contour' and 'custom': the filters whose copy needs an RGBA pass, not a CSS filter. */
export declare const isPixelFilter: (filter: string) => boolean;

/** Null for a CSS filter or none. */
export declare const readFilterInput: (image: CanvasImageSource & { width: number; height: number },
  filter: string, color: string | null) => FilterInput | null;

/** The CSS colour `filter` (tinted `tint`) paints a solid `hex` fill in; `hex` for none or when unreadable. */
export declare const filteredFill: (hex: string, filter: string, tint: string | null) => string;

export declare class ImageFilterCanvas {
  /** The cached copy for exactly (image, filter, color), else null. */
  cached(image: CanvasImageSource, filter: string, color: string | null): FilterLayer | null;
  /** Offscreen copy of `image` with `filter` applied, cached on (image, filter, color); null for 'none'. */
  canvasFor(image: CanvasImageSource & { width: number; height: number },
            filter: string, color: string | null): FilterLayer | null;
  /** Caches a copy the image worker painted for exactly this key. */
  adopt(image: CanvasImageSource, filter: string, color: string | null, layer: FilterLayer): void;
}
