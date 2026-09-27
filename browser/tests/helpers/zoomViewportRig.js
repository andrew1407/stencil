// A canvas viewport modelled the way the browser lays it out, for the ZoomPan specs: centred by
// auto margins while the picture fits, the scroll offset clamped to the real range, fullscreen
// on (the frame fixed on both axes) unless a spec turns it off.
import { ZoomPan, canvasOrigin } from '../../js/core/zoom/pan.js';

export const installViewport = ({ vpW = 1000, vpH = 700, imgW = 600, imgH = 400, scale = 1 } = {}) => {
  const canvas = { width: imgW, height: imgH, style: {}, classList: { add() {}, remove() {} } };
  const shown = (side) => parseFloat(canvas.style[side]) || 0;
  const clamp = (v, max) => Math.max(0, Math.min(v, Math.max(0, max)));
  const vp = {
    clientWidth: vpW, clientHeight: vpH, style: {}, sl: 0, st: 0,
    get scrollWidth() { return Math.max(vpW, shown('width')); },
    get scrollHeight() { return Math.max(vpH, shown('height')); },
    get scrollLeft() { return this.sl; },
    set scrollLeft(v) { this.sl = clamp(v, this.scrollWidth - vpW); },
    get scrollTop() { return this.st; },
    set scrollTop(v) { this.st = clamp(v, this.scrollHeight - vpH); },
  };
  // `margin: auto`, as the browser resolves it: half the free space, nothing when negative.
  const container = {
    get offsetLeft() { return Math.max(0, (vpW - shown('width')) / 2); },
    get offsetTop() { return Math.max(0, (vpH - shown('height')) / 2); },
  };
  globalThis.document = {
    getElementById: (id) => (id === 'canvas-viewport' ? vp : id === 'canvas-container' ? container : null),
    querySelectorAll: () => [],
    body: { classList: { contains: (c) => c === 'fullscreen-mode' } },
    activeElement: null,
  };
  globalThis.window = { innerWidth: vpW, innerHeight: vpH };
  globalThis.getComputedStyle = () => ({ borderTopWidth: '0px', borderBottomWidth: '0px',
                                         paddingTop: '0px', paddingBottom: '0px' });
  // Run the zoom animation straight to its final frame: the landing place is what matters.
  globalThis.performance = { now: () => 0 };
  globalThis.requestAnimationFrame = (cb) => { cb(1e6); return 1; };
  globalThis.cancelAnimationFrame = () => {};
  const app = { image: { width: imgW, height: imgH }, canvas, scale, storage: { save() {} } };
  const zp = new ZoomPan(app);
  zp.setZoom(scale, false);            // lay the canvas out at the starting zoom
  return { app, vp, zp };
};

// Where an image point sits inside the frame right now (0 = the frame's left/top edge).
export const seenAt = (app, vp, x, y) => ({
  x: canvasOrigin().x + x * app.scale - vp.scrollLeft,
  y: canvasOrigin().y + y * app.scale - vp.scrollTop,
});
