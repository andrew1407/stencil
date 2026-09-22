// The ink of a mark that paints no box of its own: its words and its inline glyph drawn onto
// a transparent canvas and read back as one alpha per cell, so the bare paper around them
// flies nothing. Desktop twin: controlReveal.cpp ctl::groupShot, a render over Qt::transparent.

// A cell needs this much of its area inked to fly; below it the cell holds a glyph's
// antialiasing, not the mark (desktop: DisintegrateMotes.cpp skips cell.alphaF() <= 0.02).
export const INK_FLOOR = 0.12;
// Under this share of inked cells nothing was measured that the eye would call a mark, so
// the caller keeps its own painter and fills the box.
const INK_MIN_SHARE = 0.02;
const VIEW = 24;   // config/icons.json draws every glyph on a 24×24 grid

// Each icons.json shape as path data, so a glyph strokes as one Path2D per node.
const SHAPE_D = {
  path: (n) => n.getAttribute('d'),
  line: (n) => `M${n.getAttribute('x1')} ${n.getAttribute('y1')}L${n.getAttribute('x2')} ${n.getAttribute('y2')}`,
  polyline: (n) => `M${n.getAttribute('points')}`,
  polygon: (n) => `M${n.getAttribute('points')}Z`,
  rect: (n) => `M${n.getAttribute('x') || 0} ${n.getAttribute('y') || 0}h${n.getAttribute('width')}` +
    `v${n.getAttribute('height')}h-${n.getAttribute('width')}Z`,
  circle: (n) => {
    const r = Number(n.getAttribute('r'));
    return `M${Number(n.getAttribute('cx')) - r} ${n.getAttribute('cy')}` +
      `a${r} ${r} 0 1 0 ${r * 2} 0a${r} ${r} 0 1 0 ${-r * 2} 0`;
  },
};
const SHAPES = Object.keys(SHAPE_D).join(',');

export const svgInk = (ctx, svg, ox, oy) => {
  const r = svg.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return;
  const box = (svg.getAttribute('viewBox') || '').split(/[\s,]+/).map(Number);
  ctx.save();
  ctx.translate(r.left - ox, r.top - oy);
  ctx.scale(r.width / (box[2] || VIEW), r.height / (box[3] || VIEW));
  ctx.lineWidth = Number(svg.getAttribute('stroke-width')) || 2;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  for (const node of svg.querySelectorAll(SHAPES)) {
    const d = SHAPE_D[node.tagName.toLowerCase()]?.(node);
    if (d) ctx.stroke(new Path2D(d));
  }
  ctx.restore();
};

// Each text node at the box a Range measures for it, in the style of the element holding it;
// `middle` sits the run in that box without needing the font's baseline.
export const textInk = (ctx, el, ox, oy, get) => {
  const range = document.createRange();
  const walk = (node) => {
    for (const kid of node.childNodes || []) {
      if (kid.tagName) {
        if (String(kid.tagName).toLowerCase() === 'svg') svgInk(ctx, kid, ox, oy);
        else walk(kid);
        continue;
      }
      if (kid.nodeType !== 3 || !String(kid.nodeValue).trim()) continue;
      range.selectNodeContents(kid);
      const r = range.getBoundingClientRect();
      if (!(r.width > 0 && r.height > 0)) continue;
      const cs = get(node);
      ctx.font = `${cs.fontStyle} ${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
      ctx.textBaseline = 'middle';
      ctx.fillText(kid.nodeValue, r.left - ox, r.top - oy + r.height / 2);
    }
  };
  walk(el);
};

// One alpha per cell, row-major, or null when the ink could not be measured — an untrusted
// mask must never thin a cloud, so the caller then flies the whole grid.
export const inkAlpha = (el, cols, rows) => {
  try {
    if (typeof document === 'undefined' || typeof Path2D !== 'function') return null;
    const r = el.getBoundingClientRect();
    if (!(r.width >= 1 && r.height >= 1 && cols > 0 && rows > 0)) return null;
    const sheet = document.createElement('canvas');
    sheet.width = Math.round(r.width);
    sheet.height = Math.round(r.height);
    const ctx = sheet.getContext('2d');
    const probe = document.createElement('canvas');
    probe.width = cols;
    probe.height = rows;
    const pc = probe.getContext('2d', { willReadFrequently: true });
    if (!ctx || !pc) return null;
    ctx.fillStyle = '#fff';
    ctx.strokeStyle = '#fff';
    textInk(ctx, el, r.left, r.top, getComputedStyle);
    pc.imageSmoothingEnabled = true;
    pc.imageSmoothingQuality = 'high';
    pc.drawImage(sheet, 0, 0, cols, rows);
    const px = pc.getImageData(0, 0, cols, rows).data;
    const out = new Float32Array(cols * rows);
    let inked = 0;
    for (let i = 0; i < out.length; i++) {
      out[i] = px[i * 4 + 3] / 255;
      if (out[i] >= INK_FLOOR) inked++;
    }
    return inked >= out.length * INK_MIN_SHARE ? out : null;
  } catch {
    return null;   // decoration only — an unmeasurable mark still dusts, as a full box
  }
};
