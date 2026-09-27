// ── Image-worker task vocabulary ────────────────────────────────
// Request: { id, task, type, quality?, …payload }; reply: { id, ok: true, blob | bitmap } or
// { id, ok: false, error }. Payloads are transferred, never copied.
export const IMAGE_TASK = Object.freeze({
  SCALE: 'scale',       // { bitmap: ImageBitmap, width, height, halve } → encoded at width×height
  CONTOUR: 'contour',   // { data: ArrayBuffer (RGBA8), width, height } → the core contour pass, encoded
  FILTER: 'filter',     // { recipe, data: ArrayBuffer (RGBA8), width, height, rgb } → filterPixels, an ImageBitmap
  RESULT: 'result',     // { bitmap: the base, width, height, lines, showLines, showPoints, pointSize } → encoded
});
