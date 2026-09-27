// Image-worker task vocabulary: a request is { id, task, type, quality?, …payload }, a reply
// { id, ok: true, blob | bitmap } or { id, ok: false, error }. Payloads are transferred, never copied.

export declare const IMAGE_TASK: Readonly<{
  /** { bitmap: ImageBitmap, width, height, halve } → the bitmap encoded at width×height. */
  SCALE: 'scale';
  /** { data: ArrayBuffer (RGBA8), width, height } → the core contour pass, encoded. */
  CONTOUR: 'contour';
  /** { recipe, data: ArrayBuffer (RGBA8), width, height, rgb } → the filterPixels pass, an ImageBitmap. */
  FILTER: 'filter';
  /** { bitmap: the base, width, height, lines, showLines, showPoints, pointSize } → the resting result, encoded. */
  RESULT: 'result';
}>;

export type ImageTask = typeof IMAGE_TASK[keyof typeof IMAGE_TASK];

export interface ScaleRequest {
  id: number; task: 'scale'; bitmap: ImageBitmap; width: number; height: number; halve: boolean;
  type: string; quality?: number;
}
export interface ContourRequest {
  id: number; task: 'contour'; data: ArrayBuffer; width: number; height: number; type: string; quality?: number;
}
export interface FilterRequest {
  id: number; task: 'filter'; recipe: import('../core/image/filterPixels.js').FilterRecipe;
  data: ArrayBuffer; width: number; height: number; rgb: { r: number; g: number; b: number };
}
export interface ResultRequest extends Omit<import('../core/draw/restingPaint.js').RestingJob, 'base'> {
  id: number; task: 'result'; bitmap: ImageBitmap; type: string;
}
export type ImageRequest = ScaleRequest | ContourRequest | FilterRequest | ResultRequest;
export type ImageReply = { id: number; ok: true; blob?: Blob; bitmap?: ImageBitmap } | { id: number; ok: false; error: string };
