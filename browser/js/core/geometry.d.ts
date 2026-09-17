// Shared geometry vocabulary for the shape files. Types only — there is no geometry.js;
// these are the two shapes that were being re-declared verbatim across the tree.
// The pixel spellings are the app's internal ones: a crop rect crosses a wire as
// {x,y,w,h} (layout.d.ts WireCropRect), never as this.

/** A point in image (or screen) pixels. */
export interface Point { x: number; y: number; }
/** A crop in rotated-original pixels — the spelling ImageModel.roundRect emits. */
export interface CropRect { x: number; y: number; width: number; height: number; }
