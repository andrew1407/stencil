import { dustEnabled } from '../motionPrefs.js';
import { resolveColour, paletteCss, styleFrame, tintOf, stopOfTint, dustMix, grainShape, headingOf, fillGrains } from '../../dust/dustCloud.js';
import { GHOST_MS, dustEase } from './canvasDustGrid.js';
import { DUST_ALPHA_LEVELS, dustParts, makeDustStage } from './canvasDustStage.js';
import { styleCode } from '../tune.js';

// One frame: the picture minus every cell that has left, plus the grains in the air. Two
// passes — every clear first, then every grain — or a clear would punch holes in grains.
// `styled` (runDust) carries the style, palette and per-grain scratch.
const drawDust = (st, parts, t, gather, ks, lvl, lvlN, styled) => {
  const { ctx, snap, cols, rows, sw, sh, sox, soy, dw, dh } = st;
  ctx.globalAlpha = 1;
  ctx.drawImage(snap, sox, soy, sw * cols, sh * rows, 0, 0, dw * cols, dh * rows);
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i];
    const pt = (t - p.delay) / Math.max(0.05, 1 - p.delay);
    styled.pts[i] = Math.max(0, Math.min(1, pt));
    let k;
    if (gather) { if (pt >= 1) { ks[i] = 0; continue; } k = pt <= 0 ? 1 : 1 - dustEase(pt); }
    else { if (pt <= 0) { ks[i] = 0; continue; } k = Math.min(1, pt); }
    ks[i] = k;
    ctx.clearRect(p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0);
  }
  const { style, palette, sf, pts, stop, fx, shapes, heads, tints, poly } = styled;
  const flush = () => {
    for (let l = 0; l < DUST_ALPHA_LEVELS; l++) {
      const n = lvlN[l];
      if (!n) continue;
      ctx.globalAlpha = (l + 1) / DUST_ALPHA_LEVELS;
      fillGrains(ctx, lvl[l], n, poly);
      lvlN[l] = 0;
    }
  };
// One grain into its alpha bucket. A falling grain accelerates (k²); a landing one is eased.
  const place = (i, p, k, l, sx, sy, scale) => {
    const s = Math.sin(Math.PI * k);
    const buf = lvl[l];
    const j = lvlN[l]++ * 5;
    buf[j] = p.hx + k * p.dx + s * p.swx + sx;
    buf[j + 1] = p.hy + (gather ? k : k * k) * p.dy + s * p.swy + sy;
    buf[j + 2] = p.r * (1 - k * 0.35) * scale;
    buf[j + 3] = shapes[i];
    buf[j + 4] = heads[i];
  };
  const tMs = t * styled.ms;
  for (let i = 0; i < parts.length; i++) {
    const k = ks[i], p = parts[i];
    if (k <= 0 || k >= 1 || p.empty) { stop[i] = -1; continue; }
    styleFrame(style, pts[i], k, p.w, p.len, tMs, sf);
    stop[i] = stopOfTint(style ? sf.mix : dustMix(p.w, 0), tints[i]);
    fx[i * 4] = sf.sx; fx[i * 4 + 1] = sf.sy; fx[i * 4 + 2] = sf.scale; fx[i * 4 + 3] = sf.glow;
  }
  for (let c = 0; c < palette.length; c++) {
    ctx.fillStyle = palette[c];
    for (let i = 0; i < parts.length; i++) {
      if (stop[i] !== c) continue;
      const l = Math.round((1 - ks[i]) * parts[i].a * fx[i * 4 + 3] * DUST_ALPHA_LEVELS) - 1;
      if (l < 0) continue;
      place(i, parts[i], ks[i], l, fx[i * 4], fx[i * 4 + 1], fx[i * 4 + 2]);
    }
    flush();
  }
};

// Build the grains once, then draw per frame.
export const runDust = (st, ms, gather) => {
  const parts = dustParts(st, gather);
  const ks = new Float32Array(parts.length);
  const lvl = Array.from({ length: DUST_ALPHA_LEVELS }, () => new Float32Array(parts.length * 5));
  const lvlN = new Int32Array(DUST_ALPHA_LEVELS);
// The palette resolved once, each grain's fixed shape and heading, and per-frame scratch.
  const style = styleCode();
  const styled = {
    style, ms, palette: paletteCss().map((css) => resolveColour(document, css)), sf: {}, poly: [],
    pts: new Float32Array(parts.length), stop: new Int8Array(parts.length), fx: new Float32Array(parts.length * 4),
    shapes: Int8Array.from(parts, (p) => grainShape(style, p.w)),
    heads: Float32Array.from(parts, (p) => headingOf(p.dx, p.dy, gather)),
    tints: Int8Array.from(parts, (p) => tintOf(p.w)),
  };
  st.run(ms, (t) => drawDust(st, parts, t, gather, ks, lvl, lvlN, styled));
};

// True only when the dust is playing: the caller hides the emptied editor only then.
export function ghostOut(canvas, { ms = GHOST_MS } = {}) {
  if (typeof document === 'undefined' || !canvas?.width || !canvas.height) return false;
  if (!dustEnabled()) return false;
  if (typeof requestAnimationFrame === 'undefined' || !canvas.parentElement) return false;
  try {
    const st = makeDustStage(canvas);
    if (!st) return false;
    runDust(st, ms, false);
    return true;
  } catch {
    return false;   // decoration only — the clear still happens
  }
}
