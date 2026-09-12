// Image-worker task vocabulary: a request is { id, task, type, quality?, …payload }, a reply
// { id, ok: true, blob } or { id, ok: false, error }. Payloads are transferred, never copied.

export declare const IMAGE_TASK: Readonly<{
  /** { bitmap: ImageBitmap, width, height, halve } → the bitmap encoded at width×height. */
  SCALE: 'scale';
  /** { data: ArrayBuffer (RGBA8), width, height } → the core contour pass, encoded. */
  CONTOUR: 'contour';
}>;

export type ImageTask = typeof IMAGE_TASK[keyof typeof IMAGE_TASK];

export interface ScaleRequest {
  id: number; task: 'scale'; bitmap: ImageBitmap; width: number; height: number; halve: boolean;
  type: string; quality?: number;
}
export interface ContourRequest {
  id: number; task: 'contour'; data: ArrayBuffer; width: number; height: number; type: string; quality?: number;
}
export type ImageRequest = ScaleRequest | ContourRequest;
export type ImageReply = { id: number; ok: true; blob: Blob } | { id: number; ok: false; error: string };
