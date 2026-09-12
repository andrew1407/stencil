// ── Image-worker task vocabulary ────────────────────────────────
// Request: { id, task, type, quality?, …payload }; reply: { id, ok: true, blob } or
// { id, ok: false, error }. Payloads are transferred, never copied.
export const IMAGE_TASK = Object.freeze({
  SCALE: 'scale',       // { bitmap: ImageBitmap, width, height, halve } → encoded at width×height
  CONTOUR: 'contour',   // { data: ArrayBuffer (RGBA8), width, height } → the core contour pass, encoded
});
