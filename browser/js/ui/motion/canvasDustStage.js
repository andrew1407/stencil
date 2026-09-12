import { STYLED_CELL_SCALE } from '../dustCloud.js';
import { DUST_CELL_PX, dustDelay, dustField, dustGrid } from './canvasDustGrid.js';
import { SWIRL_MAX_PX, SWIRL_SHARE, tileNoise } from './tiles.js';
import { TUNE, styleCode } from './tune.js';
export const pinDustStage = (stage, host, baseLeft = 0, baseTop = 0) => {
  const onScroll = () => {
    stage.style.left = `${baseLeft + (host.scrollLeft || 0)}px`;
    stage.style.top = `${baseTop + (host.scrollTop || 0)}px`;
  };
  host.addEventListener?.('scroll', onScroll, { passive: true });
  return () => host.removeEventListener?.('scroll', onScroll);
};

// One dust stage over a canvas' visible slice, shared by ghostOut/ghostIn.
export const makeDustStage = (canvas) => {
  const field = dustField(canvas);
  if (!field) return null;
  const { r, vis } = field;
  const { cols, rows } = dustGrid(vis.width, vis.height, DUST_CELL_PX * (styleCode() ? STYLED_CELL_SCALE : 1));
// Snapshotted once: ghostOut clears the real canvas moments later.
  const snap = document.createElement('canvas');
  snap.width = canvas.width;
  snap.height = canvas.height;
  const snapCtx = snap.getContext('2d');
  snapCtx.drawImage(canvas, 0, 0);
  const dpr = window.devicePixelRatio || 1;
  const stage = document.createElement('canvas');
  stage.className = 'canvas-dust';
  stage.width = Math.round(vis.width * dpr);
  stage.height = Math.round(vis.height * dpr);
  stage.style.left = `${field.left}px`;
  stage.style.top = `${field.top}px`;
  stage.style.width = `${vis.width}px`;
  stage.style.height = `${vis.height}px`;
  field.host.appendChild(stage);
  const unpin = pinDustStage(stage, field.host,
    field.left - (field.host.scrollLeft || 0), field.top - (field.host.scrollTop || 0));
  const finish = () => { unpin(); stage.remove(); };
  const ctx = stage.getContext('2d');
  ctx.scale(dpr, dpr);
// Sources are bitmap pixels, the visible box screen pixels: at zoom they differ.
  const kx = snap.width / r.width, ky = snap.height / r.height;
  return {
    snap, snapCtx, ctx, finish, cols, rows,
    sw: (vis.width / cols) * kx, sh: (vis.height / rows) * ky,
    sox: (vis.left - r.left) * kx, soy: (vis.top - r.top) * ky,
    dw: vis.width / cols, dh: vis.height / rows,
// `draw(t)` per rAF frame over `ms`; the timeout covers a throttled rAF.
    run(ms, draw) {
      const started = performance.now();
      const step = (now) => {
        const t = (now - started) / ms;
        if (t >= 1) { finish(); return; }
        ctx.clearRect(0, 0, vis.width, vis.height);
        draw(t);
        requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
      setTimeout(finish, ms + 400);
    },
  };
};

// Every cell's colour in ONE read: the slice is drawn down to a cols×rows canvas and read
// back once. A cell with no paint (the transparent page margin) flies nothing.
const sampleDustColours = (st) => {
  const { snap, cols, rows, sw, sh, sox, soy } = st;
  const probe = document.createElement('canvas');
  probe.width = cols;
  probe.height = rows;
  const pc = probe.getContext('2d', { willReadFrequently: true });
  pc.imageSmoothingEnabled = true;
  pc.imageSmoothingQuality = 'high';
  pc.drawImage(snap, sox, soy, sw * cols, sh * rows, 0, 0, cols, rows);
  return pc.getImageData(0, 0, cols, rows).data;
};

// Grains of one colour AND one alpha step batch into a single fill; eight steps on a 3px
// grain are below what the eye resolves.
export const DUST_ALPHA_LEVELS = TUNE.DUST_ALPHA_LEVELS;

// Painted from the accent palette, not the picture's own pixels.
export const dustParts = (st, gather) => {
  const { cols, rows, dw, dh } = st;
  const px = sampleDustColours(st);
  const parts = [];
  for (let cy = 0; cy < rows; cy++) {
    for (let cx = 0; cx < cols; cx++) {
      const n = tileNoise(cx, cy);
// Decorrelated from `n`: one hash for fall and drift slid the picture apart in sheets.
      const m = tileNoise(cx + 41, cy + 17);
      const q = tileNoise(cx + 97, cy + 53);
      const i = (cy * cols + cx) * 4;
      const a = px[i + 3] / 255;
      const dx = gather ? (n - 0.5) * 26 : (m - 0.5) * 58;
      const dy = 26 + n * 46;
      const len = Math.hypot(dx, dy);
      const amp = (q - 0.5) * 2 * Math.min(len * SWIRL_SHARE, SWIRL_MAX_PX);
// Cell bounds round so neighbours share an edge, but the OUTER edge rounds up: the picture
// is blitted at its fractional size, and rounding down left a hairline no clear reached.
      parts.push({
        x0: Math.round(cx * dw), y0: Math.round(cy * dh),
        x1: cx === cols - 1 ? Math.ceil(cols * dw) : Math.round((cx + 1) * dw),
        y1: cy === rows - 1 ? Math.ceil(rows * dh) : Math.round((cy + 1) * dh),
        hx: (cx + 0.5) * dw, hy: (cy + 0.5) * dh,
        r: (Math.min(dw, dh) / 2) * (0.62 + n * 0.5),
        delay: dustDelay(cy, rows, n, gather),
        dx, dy, len,
        swx: -(dy / len) * amp, swy: (dx / len) * amp,
        w: tileNoise(cx + 13, cy + 71),   // its own hash, for a water / fire ghost's styleFrame
        a,
        empty: a < 0.04,
      });
    }
  }
  return parts;
};
