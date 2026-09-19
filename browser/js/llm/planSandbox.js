// ── Sandboxing variants / ask previews ──────────────────────────
// Ops run against the LIVE facade (§13: never new editor logic), so a sandbox is a
// snapshot plus an exact inverse: crop/rotate are undone through the model (the original
// bitmap never changes), settings and lines are written back. Only `blank` and `frame`
// replace the original, and only they reload the pixel snapshot.
import { SCHEMA } from './planSchema.js';

// The layout line fields the registry declares — what a preview snapshot copies back.
const LINE_FIELDS = Object.keys(SCHEMA.ops.get('layout').keys.lines.items.fields);

const rectOf = (r) => (r ? { x: r.x, y: r.y, w: r.w ?? r.width, h: r.h ?? r.height } : null);
const sameRect = (a, b) => !!a && !!b && a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h;

export const captureEditorState = (stencil) => {
  return {
    filter: stencil.filter,
    filterColor: stencil.filterColor,
    pageSize: stencil.pageSize,
    allowFormulas: stencil.allowFormulas,
    formulaX: stencil.formulaX,
    formulaY: stencil.formulaY,
    // Read from `lines` (the LIVE list), not `layout` — that getter is the persisted project
    // layout and goes stale. Copied to plain data: facade proxies dereference to nothing.
    lines: (stencil.lines || []).map((l) => {
      const out = { points: (l.points || []).map((p) => ({ x: p.x, y: p.y })) };
      for (const k of LINE_FIELDS) if (k !== 'points' && l[k] != null) out[k] = l[k];
      return out;
    }),
    size: stencil.imageSize,
    cropRect: rectOf(stencil.cropRect),
  };
};

const nextFrame = () => new Promise((r) => (typeof requestAnimationFrame === 'function' ? requestAnimationFrame(r) : setTimeout(r, 16)));

const lineCount = (stencil) => (stencil.lines || []).length;

// Silent install: no replace prompt, no "pasted" toast, and out of undo — this is
// putting the user's OWN lines back after a sandboxed run, not a new edit.
const applyLines = (stencil, s) => { stencil.setLines(s.lines, { history: false }); };

const applySettings = (stencil, s) => {
  stencil.apply({
    filter: s.filter, filterColor: s.filterColor, pageSize: s.pageSize,
    allowFormulas: s.allowFormulas, formulaX: s.formulaX, formulaY: s.formulaY,
  });
};

// load() clears the line list a few frames AFTER it resolves; installing lines while some
// still exist raises the editor's "Replace layout?" prompt.
const settleLinesAfterReload = async (stencil, s) => {
  for (let i = 0; i < 40 && lineCount(stencil) !== 0; i++) await nextFrame();
  if (!s.lines.length) return;
  applyLines(stencil, s);
  // Hold it across the rest of the settle window, re-asserting only from empty.
  for (let i = 0, stable = 0; i < 40 && stable < 8; i++) {
    if (lineCount(stencil) === s.lines.length) stable++;
    else { if (lineCount(stencil) === 0) applyLines(stencil, s); stable = 0; }
    await nextFrame();
  }
};

// exportImage() renders the filter AND the lines into the pixels, so a snapshot taken for
// RESTORING must have both switched off.
export const capturePixels = async (stencil, exportImage) => {
  const { filter, showPoints, showLines } = stencil;
  stencil.apply({ filter: 'none', showPoints: false, showLines: false });
  try {
    return await exportImage();
  } finally {
    stencil.apply({ filter, showPoints, showLines });
  }
};

// Ops that swap the ORIGINAL bitmap out: only these need the pixel snapshot back.
const REPLACES_ORIGINAL = new Set(['blank', 'frame']);
export const needsPixelSnapshot = (actions) => (actions || []).some((a) => REPLACES_ORIGINAL.has(a.op));

// Ops that move the lines (crop rescales, rotate turns, layout replaces).
const TOUCHES_LINES = new Set(['crop', 'rotate', 'layout']);

// Net clockwise quarter turns the executed rotates left behind (0..3).
const netTurns = (actions) => {
  let q = 0;
  for (const a of actions) if (a.op === 'rotate') q += (a.dir === 'right' ? 1 : -1) * (a.times || 1);
  return ((q % 4) + 4) % 4;
};

// `actions` are the ops that actually RAN. Rotates are turned back and the crop re-committed
// to the saved rect — both exact on the untouched original — so no frame settles.
export const restoreWorkingImage = async (stencil, pixels, state, actions) => {
  const ran = actions || [];
  if (needsPixelSnapshot(ran)) {
    await stencil.load(pixels);
    applySettings(stencil, state);
    await settleLinesAfterReload(stencil, state);
    return;
  }
  const q = netTurns(ran);
  if (q === 3) stencil.rotateRight();
  else for (let i = 0; i < q; i++) stencil.rotateLeft();
  const r = state.cropRect;
  if (r && !sameRect(rectOf(stencil.cropRect), r)) {
    stencil.crop({ x1: `${r.x}px`, x2: `${r.x + r.w}px`, y1: `${r.y}px`, y2: `${r.y + r.h}px` });
  }
  applySettings(stencil, state);
  // Lines are written back only when the ops disturbed them — never as UI noise.
  const live = stencil.lines || [];
  if (ran.some((a) => TOUCHES_LINES.has(a.op)) && (live.length || state.lines.length)) applyLines(stencil, state);
};
