// A byte port of browser/js/ui/dust/toastGlow.js (tests/portParity.test.js): only this header and the
// import paths may differ.
import { beatAt } from './stagePaint.js';
import { STAGE } from './stageRules.js';

const MARGIN = 30;        // canvas room around the pill for the halo
const RINGS = 8;          // expanding outlines stand in for a blur (desktop IdleCard does the same)
const REACH = 17;         // px the halo reaches past the pill at full breath
const ALPHA = 0.3;        // the innermost ring at full breath
const RADIUS = 8;         // the pill's own corner

const rgba = (hex, a) => {
  const n = parseInt(String(hex).replace('#', ''), 16);
  return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${a})`;
};

// One breath of the halo around a w×h pill at (x, y), drawn outward from its edge.
export const paintToastGlow = (ctx, x, y, w, h, beat) => {
  const reach = REACH * (0.45 + 0.55 * beat);
  ctx.lineJoin = 'round';
  for (let i = RINGS; i >= 1; i--) {
    const t = i / RINGS;
    const grow = reach * t;
    ctx.strokeStyle = rgba(STAGE.toastGlow, ALPHA * (1 - t) * (0.5 + 0.5 * beat));
    ctx.lineWidth = (reach / RINGS) * 2;
    ctx.beginPath();
    const r = RADIUS + grow;
    const bx = x - grow, by = y - grow, bw = w + 2 * grow, bh = h + 2 * grow;
    if (ctx.roundRect) ctx.roundRect(bx, by, bw, bh, r);
    else ctx.rect(bx, by, bw, bh);
    ctx.stroke();
  }
};

// Runs for the toast's life; the caller stops it when the toast leaves.
export const attachToastGlow = (toast, doc = globalThis.document) => {
  const stop = () => {};
  if (!toast?.getBoundingClientRect || !doc?.createElement) return stop;
  const canvas = doc.createElement('canvas');
  canvas.className = 'toast-glow';
  canvas.setAttribute('aria-hidden', 'true');
  const ctx = canvas.getContext?.('2d');
  if (!ctx) return stop;
  doc.body.appendChild(canvas);

  const raf = globalThis.requestAnimationFrame ?? ((fn) => setTimeout(() => fn(), 16));
  const cancel = globalThis.cancelAnimationFrame ?? clearTimeout;
  const now = () => (globalThis.performance?.now?.() ?? Date.now());
  const started = now();
  let frame = null, live = true;

  const step = () => {
    frame = null;
    if (!live || !toast.isConnected) { canvas.remove(); return; }
    const r = toast.getBoundingClientRect();
    const w = r.width + 2 * MARGIN, h = r.height + 2 * MARGIN;
    const dpr = Math.min(globalThis.devicePixelRatio || 1, 2);
    if (canvas.width !== Math.round(w * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      canvas.style.width = `${w}px`;
      canvas.style.height = `${h}px`;
    }
    canvas.style.transform = `translate(${r.left - MARGIN}px, ${r.top - MARGIN}px)`;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    // The pill rides in and out on surfaceIn/surfaceOut; the halo wears that same opacity, or
    // it would hang lit in mid-air before the notice had arrived.
    ctx.globalAlpha = Number(globalThis.getComputedStyle?.(toast)?.opacity ?? 1);
    paintToastGlow(ctx, MARGIN, MARGIN, r.width, r.height, beatAt(now() - started));
    ctx.globalAlpha = 1;
    frame = raf(step);
  };
  step();

  return () => {
    live = false;
    if (frame) cancel(frame);
    canvas.remove();
  };
};
