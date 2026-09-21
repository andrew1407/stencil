// The logo stage's painter: backdrop, the accent glow and the spoke ring (css/animations/
// logoHover.css logoPulse / logoRaysSpin / logoRaysShimmer at stage scale), then the mark.
// Desktop twin: app/LogoStagePaint.cpp.
import { faviconSvg, accentHex, normalizeHex } from '../../core/settings/accents.js';
import { STAGE } from './stageRules.js';

// 0 → 1 → 0 on the beat, the cosine the CSS keyframes ride.
export const beatAt = (tMs, ms = STAGE.stage.beatMs) => 0.5 - 0.5 * Math.cos((2 * Math.PI * tMs) / ms);
export const spinAt = (tMs, ms = STAGE.stage.spinMs) => ((tMs % ms) / ms) * 2 * Math.PI;

// The mark as an <img>, redrawn whenever the accent moves. Its frame is the accent, so the
// stage's copy tracks the toolbar's.
export const stageMark = (doc, hex) => {
  const img = doc.createElement('img');
  img.decoding = 'sync';
  img.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(faviconSvg(hex))}`;
  return img;
};

export const markHex = (app) => normalizeHex(app?.customAccent) || accentHex(app?.accent) || '#7c3aed';

const rgba = (hex, alpha) => {
  const h = normalizeHex(hex) || '#7c3aed';
  const n = parseInt(h.slice(1), 16);
  return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${alpha})`;
};

export const paintBackdrop = (ctx, w, h, fade) => {
  ctx.clearRect(0, 0, w, h);
  ctx.globalAlpha = fade;
  ctx.fillStyle = `rgba(0, 0, 0, ${STAGE.stage.scrimAlpha})`;
  ctx.fillRect(0, 0, w, h);
  ctx.globalAlpha = 1;
};

// The halo: solid to the mark's edge, then falling off. It follows the mark's OWN rounded square
// (the toast's shining does the same for its pill) — a circle round a square leaves the four
// corners unlit and the light reading as a disc behind the art rather than a glow off it.
// A hold widens its REACH as well as its brightness; that reach is what reads as intensity.
export const paintGlow = (ctx, { x, y, size, hex, beat, boost }) => {
  const half = size * STAGE.stage.markEdgeShare;
  const reach = STAGE.glow.reachShare * size * beat * boost;
  if (!(half > 0)) return;
  // Capped at the show's own ceiling, never 1: opaque, the light stops reading as light and the
  // window behind it disappears. A hold's intensity is carried by the REACH.
  const a = Math.min(STAGE.glow.alphaMax,
    (STAGE.glow.alphaMin + (STAGE.glow.alphaMax - STAGE.glow.alphaMin) * beat) * boost);
  const corner = size * STAGE.stage.markCornerShare;
  const edge = (grow) => {
    const s = half + grow;
    if (ctx.roundRect) ctx.roundRect(x - s, y - s, 2 * s, 2 * s, corner + grow);
    else ctx.rect(x - s, y - s, 2 * s, 2 * s);
  };
  // ONE soft-edged fill — a blur of the mark's own shape, so the light follows it and fades out
  // smoothly. Stacked fills gave the same falloff but cost a pass over the whole halo EACH.
  if (reach > 0) {
    ctx.save();
    ctx.filter = `blur(${reach / 2}px)`;
    ctx.fillStyle = rgba(hex, a);
    ctx.beginPath();
    edge(reach / 2);
    ctx.fill();
    ctx.restore();
  }
  // …and a crisp core, so the light is solid right up to the mark that covers it.
  ctx.fillStyle = rgba(hex, a);
  ctx.beginPath();
  edge(0);
  ctx.fill();
};

// The ring: evenly spaced spokes just outside the mark, turning on the spin and breathing on the beat.
export const paintSpokes = (ctx, { x, y, size, hex, beat, angle, boost }) => {
  const { spokes, gapShare, lengthShare, alphaMin, alphaMax, softWidthShare, brightWidthShare } = STAGE.sun;
  const a = Math.min(1, (alphaMin + (alphaMax - alphaMin) * beat) * boost);
  const r1 = size * 0.5 + gapShare * size;
  const r2 = r1 + lengthShare * size;
  ctx.lineCap = 'round';
  for (const [width, alpha] of [[softWidthShare * size, a * 0.45], [brightWidthShare * size, a]]) {
    ctx.strokeStyle = rgba(hex, alpha);
    ctx.lineWidth = Math.max(1, width);
    ctx.beginPath();
    for (let i = 0; i < spokes; i++) {
      const t = angle + (i / spokes) * 2 * Math.PI;
      const c = Math.cos(t), s = Math.sin(t);
      ctx.moveTo(x + c * r1, y + s * r1);
      ctx.lineTo(x + c * r2, y + s * r2);
    }
    ctx.stroke();
  }
};

// The mark sits still — only its light breathes. A show that changes size says so itself.
export const paintMark = (ctx, img, { x, y, size }) => {
  if (!img?.width) return;
  ctx.drawImage(img, x - size / 2, y - size / 2, size, size);
};
