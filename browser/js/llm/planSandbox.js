// ── Sandboxing variants / ask previews ──────────────────────────
// Both run their ops against the LIVE editor and then put it back. `load()` restores pixels
// ONLY — it leaves variant-touched settings and CLEARS the lines — so state is captured too.
import { SCHEMA } from './planSchema.js';

// The layout line fields the registry declares — what a preview snapshot copies back.
const LINE_FIELDS = Object.keys(SCHEMA.ops.get('layout').keys.lines.items.fields);

export const captureEditorState = (stencil) => {
  return {
    filter: stencil.filter,
    filterColor: stencil.filterColor,
    pageSize: stencil.pageSize,
    allowFormulas: stencil.allowFormulas,
    formulaX: stencil.formulaX,
    formulaY: stencil.formulaY,
    // Read from `lines` (the LIVE list), not `layout` — that getter is the persisted
    // project layout and goes stale. Copied to plain data: the facade hands back proxies
    // into app.lines, which dereference to nothing once load() clears them.
    lines: (stencil.lines || []).map((l) => {
      const out = { points: (l.points || []).map((p) => ({ x: p.x, y: p.y })) };
      for (const k of LINE_FIELDS) if (k !== 'points' && l[k] != null) out[k] = l[k];
      return out;
    }),
    size: stencil.imageSize,
  };
};

const nextFrame = () => new Promise((r) => (typeof requestAnimationFrame === 'function' ? requestAnimationFrame(r) : setTimeout(r, 16)));

const lineCount = (stencil) => (stencil.lines || []).length;

// Silent install: no replace prompt, no "pasted" toast, and out of undo — this is
// putting the user's OWN lines back after a sandboxed run, not a new edit.
const applyLines = (stencil, s) => { stencil.setLines(s.lines, { history: false }); };

const restoreEditorState = async (stencil, s, reloaded) => {
  stencil.apply({
    filter: s.filter, filterColor: s.filterColor, pageSize: s.pageSize,
    allowFormulas: s.allowFormulas, formulaX: s.formulaX, formulaY: s.formulaY,
  });
  // Lines are only touched when the ops disturbed them; otherwise they are already
  // right and writing them again would be pointless UI noise.
  if (!reloaded) return;
  // load() clears the line list a few frames AFTER it resolves. Wait for that, because
  // installing lines while some still exist raises the editor's "Replace layout?"
  // prompt — this is an internal restore, never a user paste.
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

// exportImage() renders the filter AND the lines into the pixels, so a snapshot taken
// for RESTORING must have both switched off — reloading a normal export would bake the
// filter in permanently and burn the annotations into the image.
export const capturePixels = async (stencil, exportImage) => {
  const { filter, showPoints, showLines } = stencil;
  stencil.apply({ filter: 'none', showPoints: false, showLines: false });
  try {
    return await exportImage();
  } finally {
    stencil.apply({ filter, showPoints, showLines });
  }
};

// Ops whose effect a settings restore alone cannot undo — the pixels, or the line list.
// A variant built only from the others (filter, page, formula) needs no reload at all,
// which keeps the common case fast and leaves the user's lines untouched.
const NEEDS_RELOAD = new Set(['crop', 'rotate', 'blank', 'frame', 'layout']);

// Put the editor back exactly as `state`/`pixels` found it. The reload is skipped when
// the ops that ran could not have touched the pixels.
export const restoreWorkingImage = async (stencil, pixels, state, actions) => {
  const reload = (actions || []).some((a) => NEEDS_RELOAD.has(a.op));
  if (reload) await stencil.load(pixels);
  await restoreEditorState(stencil, state, reload);
};