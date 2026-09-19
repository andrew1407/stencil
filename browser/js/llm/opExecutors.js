// ── The op executors: one per registered op (contract §13) ──────
// run(a, ctx) executes a VALIDATED action against the frozen window.stencil facade — never
// new editor logic. Everything else about an op is its registry entry's.
import { defaultBlankSizePx } from '../core/layout.js';
import { clamp } from '../utils/math.js';
import { SCHEMA } from './planSchema.js';
import { composeFrame, mapFramePoint } from './frame.js';
import { SETTINGS_RUN } from './settingsExecutors.js';

// ── Browser-side normalizers (the few outputs the generic deep-pick can't express) ──
// They run on top of normalize(), which already trims `trim` keys and applies defaults.
const NORMALIZE = {
  // "" after trimming is no destination at all.
  save: (out) => { if (out.path === '') delete out.path; return out; },
  // A preset persists by its lowercase key; the default "image" form is the bare op.
  accent: (out) => { if (out.preset != null) out.preset = out.preset.toLowerCase(); return out; },
  copy: (out) => (out.what === 'layout' ? out : { op: 'copy' }),
};

const IMAGE_RUN = {
  crop: (a, { stencil, frame }) => {
    // cropRect before/after are both in rotated-original px, so their origin delta IS the rect's
    // origin in the pre-crop frame: later plan coords shift by its negation (§1).
    const before = frame && stencil.cropRect;
    stencil.crop(a.spec);
    const after = frame && stencil.cropRect;
    if (before && after) {
      composeFrame(frame, { a: 1, b: 0, c: 0, d: 1, tx: -(after.x - before.x), ty: -(after.y - before.y) });
    }
  },
  rotate: (a, { stencil, frame }) => {
    for (let i = 0; i < a.times; i++) {
      const size = stencil.imageSize;   // pre-turn W×H — the box the points rotate in
      (a.dir === 'left' ? stencil.rotateLeft() : stencil.rotateRight());
      // Same quarter-turn the editor applies to existing points
      // (cropGeometry rotateLinePointsQuarter): right x'=H−y,y'=x; left x'=y,y'=W−x.
      if (frame && size) {
        composeFrame(frame, a.dir === 'right'
          ? { a: 0, b: -1, c: 1, d: 0, tx: size.height, ty: 0 }
          : { a: 0, b: 1, c: -1, d: 0, tx: 0, ty: size.width });
      }
    }
  },
  filter: (a, { stencil }) => {
    stencil.apply(a.mode === 'custom' ? { filter: 'custom', filterColor: a.tint } : { filter: a.mode });
  },
  layout: (a, { stencil, frame }) => {
    // setLines(), not `stencil.layout =`: the latter is the clipboard-paste path and prompts
    // Combine / Replace / Cancel once the image carries lines — a dialog a plan cannot answer.
    if (!stencil.imageSize) throw new Error('Layout actions need a working image to draw on');
    // §1: re-map each point through the plan's accumulated crop/rotate transform, then clamp into
    // the working image — even at identity, so model points can never draw outside the picture.
    const { width, height } = stencil.imageSize;
    const lines = a.lines.map((l) => ({
      ...l,
      points: l.points.map((p) => {
        const q = frame ? mapFramePoint(frame, p) : p;
        return { x: clamp(q.x, 0, width), y: clamp(q.y, 0, height) };
      }),
    }));
    stencil.setLines(lines);
  },
  formula: (a, { stencil }) => {
    if (a.enabled != null) { stencil.apply({ allowFormulas: a.enabled }); return; }
    const key = a.axis === 'x' ? 'formulaX' : 'formulaY';
    // An empty expr clears the axis without switching formulas on for it.
    stencil.apply(a.expr.trim() ? { allowFormulas: true, [key]: a.expr } : { [key]: '' });
  },
  page: (a, { stencil }) => {
    if (a.format) { stencil.apply({ page: a.format }); return; }
    // Custom dims map onto the editors' custom page size (contract §2): the two
    // cm setters, then the 'custom' page format — the same controls' paths.
    stencil.pageWidth = a.width;
    stencil.pageHeight = a.height;
    stencil.apply({ page: 'custom' });
  },
  blank: async (a, { stencil }) => {
    if (a.format) stencil.apply({ page: a.format });
    // Explicit cm dims override the format: rendered at the same 96 dpi a page-
    // format blank gets (core defaultBlankSizePx), passed as the pixel size opts.
    if (a.width != null) await stencil.blank(a.color, { size: defaultBlankSizePx({ width: a.width, height: a.height }) });
    else await stencil.blank(a.color);
  },
  // §2 undo/redo, top-level only: sandboxed variant and preview renders write history-invisible
  // state, so stepping history from inside one would tear the sandbox open.
  undo: (a, { stencil }) => { for (let i = 0; i < a.steps; i++) stencil.undo(); },
  redo: (a, { stencil }) => { for (let i = 0; i < a.steps; i++) stencil.redo(); },
  frame: async (a, { exportImage, loadFrame, results }) => {
    if (!loadFrame) throw new Error('The current input is not a video — the "frame" operation needs a video input');
    if (a.index != null) { await loadFrame(a.index); return; }
    // Multiple indices → one extra output image per frame (like variants).
    for (const idx of a.indices) {
      await loadFrame(idx);
      if (exportImage) results.push({ label: `frame${idx}`, dataUrl: await exportImage() });
    }
  },
};

// The §2.1 / §10 half of the table lives next door; one registry is built from both.
const RUN = { ...IMAGE_RUN, ...SETTINGS_RUN };

// The op registry, one entry per whitelisted op (§13), in registry order — which IS the
// prompt's bullet order. `editorSetting` marks §10 ops, forbidden in variants.
export const OPS = {};
for (const entry of SCHEMA.entries) {
  if (!RUN[entry.name]) throw new Error(`opRegistry: the browser registers "${entry.name}" but has no executor for it`);
  const def = {
    bullet: entry.bullet ?? null,
    validate(a) {
      const out = SCHEMA.normalize(SCHEMA.validateAction(a, entry), entry);
      return NORMALIZE[entry.name] ? NORMALIZE[entry.name](out) : out;
    },
    run: RUN[entry.name],
  };
  if (entry.also) { def.also = entry.also; def.alsoOrder = entry.alsoOrder; }
  if (entry.requires) def.requires = entry.requires.slice();
  for (const flag of ['editorSetting', 'topLevelOnly', 'newFrame', 'deferred']) {
    if (entry.flags && entry.flags[flag]) def[flag] = true;
  }
  OPS[entry.name] = def;
}
for (const name of Object.keys(RUN)) {
  if (!OPS[name]) throw new Error(`opRegistry: executor "${name}" has no browser registry entry`);
}
