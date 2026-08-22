// ── Op-plan: system prompt, parser/validator, and executor (llm-contract.md §1–4) ──
// Pure module — no DOM, no fetch. The LLM never touches pixels: it emits a strictly
// validated plan of whitelisted ops that executeOpPlan maps 1:1 onto the same
// window.stencil facade calls the toolbar uses. LLM output is data, not instructions.
import { defaultBlankSizePx } from '../core/layout.js';
import PROMPT_ASSET from '../config/llm/systemPrompt.json' with { type: 'json' };

// Canonical system prompt (contract §4 + §13): the PROSE CORE (head+tail from the shared
// config/llm/systemPrompt.json asset) is embedded verbatim — append-only, never prepend —
// while the "Available ops" section and the §10 settings block are ASSEMBLED from the OPS
// registry, so the prompt can never promise an op this surface cannot run.
export const PROMPT_CORE_HEAD = PROMPT_ASSET.head;
export const PROMPT_CORE_TAIL = PROMPT_ASSET.tail;

// Limits — the same numbers in every client (contract §1).
export const LIMITS = { actions: 16, variants: 8, layoutLines: 200, frameIndices: 32, stringChars: 5000, pathChars: 1024 };

// §11 interactive replies. The option cap is what a choice card can show without becoming a
// menu; question/label/answer caps keep a model-written card from filling the transcript.
export const ASK_LIMITS = { minOptions: 2, maxOptions: 5, question: 300, label: 80, answer: 500 };
export const DEFAULT_CUSTOM_LABEL = 'Something else…';

// §10 lineStyle ranges = the toolbar inputs' ranges.
const THICKNESS_MIN = 1, THICKNESS_MAX = 20;
const POINT_SIZE_MIN = 1, POINT_SIZE_MAX = 30;
// §2 page/blank custom dims (centimetres) and the §2 undo/redo step bound.
const DIM_CM_MIN = 0.1, DIM_CM_MAX = 500;
const UNDO_STEPS_MAX = 20;
// §10 zoom = the toolbar zoom input's range; compare split = the divider's clamp.
const ZOOM_MIN = 5, ZOOM_MAX = 3200;
const SPLIT_MIN = 0.02, SPLIT_MAX = 0.98;
const COMPARE_MODES = new Set(['none', 'original', 'vertical', 'horizontal']);

const HEX = /^#[0-9a-fA-F]{6}$/;
const CSS_NAME = /^[a-zA-Z]+$/;
const CROP_TOKEN = /^-?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?$/;
// Strict W:H — digits only, both positive (mirrors core/parse/cropSpec.cpp).
const CROP_ASPECT = /^(\d+):(\d+)$/;
const PAGE_FORMAT = /^[abc](10|[0-9])$/;   // lowercase ISO names a0…c10
const STYLES = new Set(['solid', 'dashed', 'dotted']);
const FILTERS = new Set(['none', 'bw', 'sepia', 'invert', 'contour', 'custom']);
const LINE_KEYS = new Set(['points', 'color', 'thickness', 'pointSize', 'style', 'locked', 'fillColor']);

// ── §1 coordinate re-mapping (executor-side) ────────────────────────────────
// Plan coordinates are in the frame of the working image the model SAW; crop/rotate change
// it mid-plan, so the executor composes a running affine map (quarter turns + integer
// translations, exact arithmetic) and pushes every later layout point through it, clamped.
const identityFrame = () => ({ a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 });

// Compose `n` AFTER `f` (points flow f → n), in place.
const composeFrame = (f, n) => {
  const { a, b, c, d, tx, ty } = f;
  f.a = n.a * a + n.b * c;
  f.b = n.a * b + n.b * d;
  f.tx = n.a * tx + n.b * ty + n.tx;
  f.c = n.c * a + n.d * c;
  f.d = n.c * b + n.d * d;
  f.ty = n.c * tx + n.d * ty + n.ty;
};

const mapFramePoint = (f, p) => ({ x: f.a * p.x + f.b * p.y + f.tx, y: f.c * p.x + f.d * p.y + f.ty });

const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
const isStr = (v, max = LIMITS.stringChars) => typeof v === 'string' && v.length <= max;
const fail = (op, why) => { throw new Error(`Invalid ${op} action: ${why}`); };

// Reject unknown fields on a KNOWN op — that fails the whole plan (contract §1).
const onlyKeys = (a, allowed) => {
  const extra = Object.keys(a).filter((k) => k !== 'op' && !allowed.includes(k));
  if (extra.length) fail(a.op, `unknown field "${extra[0]}"`);
};

// ── The op registry: one entry per whitelisted op (contract §13) ──
// validate(a) → cleaned action or throw; run(a, ctx) executes against the frozen facade.
// editorSetting marks §10 ops (adjust the EDITOR, not the image — forbidden in variants).
// bullet = the entry's "Available ops" prompt line (registry order IS prompt order; a
// shared bullet lives on the first op). also = §10 "also accepts" line (alsoOrder).
// requires = capabilities assemblePrompts() must see wired before promising the op (§13).
export const OPS = {
  crop: {
    bullet: `- {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
  tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
  opposite side. Include only the edges you want to move. For a target aspect ratio add
  "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
  square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
  so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
  region should be kept.`,
    validate(a) {
      onlyKeys(a, ['spec', 'aspect']);
      if (!isObj(a.spec)) fail('crop', '"spec" must be an object');
      // §3.2 action-level aspect tolerance: models sometimes put "aspect" beside
      // "spec" — fold it in when the spec lacks it; a conflicting duplicate fails.
      const src = { ...a.spec };
      if (a.aspect != null) {
        if (src.aspect != null && src.aspect !== a.aspect) fail('crop', '"aspect" appears both beside "spec" and inside it with different values');
        if (src.aspect == null) src.aspect = a.aspect;
      }
      const keys = Object.keys(src);
      const bad = keys.find((k) => !['x1', 'x2', 'y1', 'y2', 'aspect'].includes(k));
      if (bad) fail('crop', `unknown spec key "${bad}"`);
      if (!keys.length) fail('crop', 'spec needs at least one of x1/x2/y1/y2/aspect');
      const spec = {};
      for (const k of keys) {
        const tok = src[k];
        if (k === 'aspect') {
          const m = isStr(tok) ? CROP_ASPECT.exec(tok) : null;
          if (!m || +m[1] <= 0 || +m[2] <= 0) fail('crop', 'bad token for "aspect"');
        } else if (!isStr(tok) || !CROP_TOKEN.test(tok)) fail('crop', `bad token for "${k}"`);
        spec[k] = tok;
      }
      return { op: 'crop', spec };
    },
    run: (a, { stencil, frame }) => {
      // The crop path resolves the spec itself; we only observe the resolved rect.
      // cropRect before/after are both in rotated-original px, so their origin delta IS
      // the rect's origin in the pre-crop frame: later plan coords shift by its negation (§1).
      const before = frame && stencil.cropRect;
      stencil.crop(a.spec);
      const after = frame && stencil.cropRect;
      if (before && after) {
        composeFrame(frame, { a: 1, b: 0, c: 0, d: 1, tx: -(after.x - before.x), ty: -(after.y - before.y) });
      }
    },
  },
  rotate: {
    bullet: '- {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.',
    validate(a) {
      onlyKeys(a, ['dir', 'times']);
      if (a.dir !== 'left' && a.dir !== 'right') fail('rotate', '"dir" must be "left" or "right"');
      const times = a.times == null ? 1 : a.times;
      if (!Number.isInteger(times) || times < 1 || times > 3) fail('rotate', '"times" must be an integer 1..3');
      return { op: 'rotate', dir: a.dir, times };
    },
    run: (a, { stencil, frame }) => {
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
  },
  filter: {
    bullet: `- {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
  — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.`,
    validate(a) {
      onlyKeys(a, ['mode', 'tint']);
      if (!FILTERS.has(a.mode)) fail('filter', `unknown mode "${a.mode}"`);
      if (a.mode === 'custom') {
        if (!isStr(a.tint) || !HEX.test(a.tint)) fail('filter', '"custom" requires "tint" as #rrggbb');
        return { op: 'filter', mode: 'custom', tint: a.tint };
      }
      if (a.tint != null) fail('filter', '"tint" is only valid with mode "custom"');
      return { op: 'filter', mode: a.mode };
    },
    run: (a, { stencil }) => {
      stencil.apply(a.mode === 'custom' ? { filter: 'custom', filterColor: a.tint } : { filter: a.mode });
    },
  },
  layout: {
    bullet: `- {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
  "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
  — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
  shapes, or structure from an attached image, answer with this op. An empty "lines"
  array REMOVES every drawn line — that is what "clear/remove the lines" means.`,
    validate(a) {
      onlyKeys(a, ['lines']);
      if (!Array.isArray(a.lines)) fail('layout', '"lines" must be an array');
      if (a.lines.length > LIMITS.layoutLines) fail('layout', `more than ${LIMITS.layoutLines} lines`);
      const lines = a.lines.map((l) => {
        if (!isObj(l)) fail('layout', 'each line must be an object');
        const bad = Object.keys(l).find((k) => !LINE_KEYS.has(k));
        if (bad) fail('layout', `unknown line field "${bad}"`);
        if (!Array.isArray(l.points)) fail('layout', 'line "points" must be an array');
        const out = {
          points: l.points.map((p) => {
            if (!isObj(p) || Object.keys(p).some((k) => k !== 'x' && k !== 'y')) fail('layout', 'points must be {x, y} objects');
            if (!Number.isFinite(p.x) || !Number.isFinite(p.y)) fail('layout', 'point coords must be finite numbers');
            return { x: p.x, y: p.y };
          }),
        };
        if (l.color != null) { if (!isStr(l.color)) fail('layout', 'line "color" must be a string'); out.color = l.color; }
        if (l.thickness != null) { if (!Number.isFinite(l.thickness)) fail('layout', 'line "thickness" must be a number'); out.thickness = l.thickness; }
        if (l.pointSize != null) { if (!Number.isFinite(l.pointSize)) fail('layout', 'line "pointSize" must be a number'); out.pointSize = l.pointSize; }
        if (l.style != null) { if (!STYLES.has(l.style)) fail('layout', `unknown line style "${l.style}"`); out.style = l.style; }
        if (l.locked != null) { if (typeof l.locked !== 'boolean') fail('layout', 'line "locked" must be a boolean'); out.locked = l.locked; }
        if (l.fillColor != null) { if (!isStr(l.fillColor)) fail('layout', 'line "fillColor" must be a string'); out.fillColor = l.fillColor; }
        return out;
      });
      return { op: 'layout', lines };
    },
    run: (a, { stencil, frame }) => {
      // setLines(), not `stencil.layout =`: the latter is the clipboard-paste path and
      // prompts Combine / Replace / Cancel once the image carries lines — a dialog a
      // plan cannot answer. Merges the current image dims itself.
      if (!stencil.imageSize) throw new Error('Layout actions need a working image to draw on');
      // §1: re-map each point through the plan's accumulated crop/rotate transform,
      // then clamp into the working image — even at identity, so out-of-frame model
      // points can never draw outside the picture. The validated action is not mutated.
      const { width, height } = stencil.imageSize;
      const clamp = (v, hi) => Math.min(Math.max(v, 0), hi);
      const lines = a.lines.map((l) => ({
        ...l,
        points: l.points.map((p) => {
          const q = frame ? mapFramePoint(frame, p) : p;
          return { x: clamp(q.x, width), y: clamp(q.y, height) };
        }),
      }));
      stencil.setLines(lines);
    },
  },
  formula: {
    bullet: `- {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
  matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
  that axis; {"op":"formula","enabled":false} switches formulas OFF entirely.`,
    validate(a) {
      onlyKeys(a, ['axis', 'expr', 'enabled']);
      // §2: `enabled` rides ALONE — false switches formulas off (restoring identity).
      if (a.enabled != null) {
        if (a.axis != null || a.expr != null) fail('formula', '"enabled" rides alone — use axis+expr OR enabled');
        if (typeof a.enabled !== 'boolean') fail('formula', '"enabled" must be a boolean');
        return { op: 'formula', enabled: a.enabled };
      }
      if (a.axis !== 'x' && a.axis !== 'y') fail('formula', '"axis" must be "x" or "y"');
      if (!isStr(a.expr)) fail('formula', '"expr" must be a string (empty clears that axis)');
      // Charset check with the single variable matching the axis; the core formula
      // engine validates the expression again before use (contract §2). An empty
      // (or whitespace) expr clears that axis.
      if (a.expr.trim()) {
        const ok = a.axis === 'x' ? /^[0-9x+\-*/(). ]+$/.test(a.expr) : /^[0-9y+\-*/(). ]+$/.test(a.expr);
        if (!ok) fail('formula', `"expr" may only use digits, + - * / ( ) . and "${a.axis}"`);
      }
      return { op: 'formula', axis: a.axis, expr: a.expr };
    },
    run: (a, { stencil }) => {
      if (a.enabled != null) { stencil.apply({ allowFormulas: a.enabled }); return; }
      const key = a.axis === 'x' ? 'formulaX' : 'formulaY';
      // An empty expr clears the axis without switching formulas on for it.
      stencil.apply(a.expr.trim() ? { allowFormulas: true, [key]: a.expr } : { [key]: '' });
    },
  },
  page: {
    bullet: `- {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
  size: {"op":"page","width":20,"height":30} in centimetres (one form or the other).`,
    validate(a) {
      onlyKeys(a, ['format', 'width', 'height']);
      // §2: exactly one form — an ISO format, or custom width+height in cm.
      const hasFormat = a.format != null, hasDims = a.width != null || a.height != null;
      if (hasFormat === hasDims) fail('page', 'exactly one of "format" / "width"+"height" is required');
      if (hasFormat) {
        if (!isStr(a.format) || !PAGE_FORMAT.test(a.format)) fail('page', '"format" must be a lowercase ISO name a0–a10, b0–b10 or c0–c10');
        return { op: 'page', format: a.format };
      }
      for (const k of ['width', 'height']) {
        if (!Number.isFinite(a[k]) || a[k] < DIM_CM_MIN || a[k] > DIM_CM_MAX) fail('page', `"${k}" must be centimetres ${DIM_CM_MIN}..${DIM_CM_MAX}`);
      }
      return { op: 'page', width: a.width, height: a.height };
    },
    run: (a, { stencil }) => {
      if (a.format) { stencil.apply({ page: a.format }); return; }
      // Custom dims map onto the editors' custom page size (contract §2): the two
      // cm setters, then the 'custom' page format — the same controls' paths.
      stencil.pageWidth = a.width;
      stencil.pageHeight = a.height;
      stencil.apply({ page: 'custom' });
    },
  },
  blank: {
    bullet: `- {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
  centimetre dims ride as "width"/"height" instead of "format".`,
    newFrame: true,   // replaces the working image — accumulated re-mapping is meaningless
    validate(a) {
      onlyKeys(a, ['color', 'format', 'width', 'height']);
      if (!isStr(a.color) || !(HEX.test(a.color) || CSS_NAME.test(a.color))) fail('blank', '"color" must be #rrggbb or a CSS color name');
      const out = { op: 'blank', color: a.color };
      if (a.format != null) {
        if (!isStr(a.format) || !PAGE_FORMAT.test(a.format)) fail('blank', 'bad page "format"');
        out.format = a.format;
      }
      // §2: explicit cm dims ride together and override the format.
      if ((a.width != null) !== (a.height != null)) fail('blank', '"width" and "height" ride together');
      if (a.width != null) {
        for (const k of ['width', 'height']) {
          if (!Number.isFinite(a[k]) || a[k] < DIM_CM_MIN || a[k] > DIM_CM_MAX) fail('blank', `"${k}" must be centimetres ${DIM_CM_MIN}..${DIM_CM_MAX}`);
        }
        out.width = a.width;
        out.height = a.height;
      }
      return out;
    },
    run: async (a, { stencil }) => {
      if (a.format) stencil.apply({ page: a.format });
      // Explicit cm dims override the format: rendered at the same 96 dpi a page-
      // format blank gets (core defaultBlankSizePx), passed as the pixel size opts.
      if (a.width != null) await stencil.blank(a.color, { size: defaultBlankSizePx({ width: a.width, height: a.height }) });
      else await stencil.blank(a.color);
    },
  },
  // §2 undo/redo: the surface's OWN edit history, one facade step per history entry.
  // Top-level only — sandboxed variant/preview renders write history-invisible state,
  // so stepping history from inside one would tear the sandbox open.
  undo: {
    // One shared bullet documents undo AND redo — it lives here, redo carries none.
    bullet: `- {"op":"undo","steps":1} / {"op":"redo","steps":1} — step this surface's edit history.
  "Undo that" means {"op":"undo"}; steps count history entries, which can be finer
  than one request.`,
    topLevelOnly: true,
    newFrame: true,   // may revert a crop/rotate — the accumulated re-mapping is stale
    validate(a) {
      onlyKeys(a, ['steps']);
      const steps = a.steps == null ? 1 : a.steps;
      if (!Number.isInteger(steps) || steps < 1 || steps > UNDO_STEPS_MAX) fail('undo', `"steps" must be an integer 1..${UNDO_STEPS_MAX}`);
      return { op: 'undo', steps };
    },
    run: (a, { stencil }) => { for (let i = 0; i < a.steps; i++) stencil.undo(); },
  },
  redo: {
    topLevelOnly: true,
    newFrame: true,
    validate(a) {
      onlyKeys(a, ['steps']);
      const steps = a.steps == null ? 1 : a.steps;
      if (!Number.isInteger(steps) || steps < 1 || steps > UNDO_STEPS_MAX) fail('redo', `"steps" must be an integer 1..${UNDO_STEPS_MAX}`);
      return { op: 'redo', steps };
    },
    run: (a, { stencil }) => { for (let i = 0; i < a.steps; i++) stencil.redo(); },
  },
  frame: {
    bullet: `- {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
  only valid when the current input is a video.`,
    newFrame: true,   // loads a fresh video frame — a new coordinate frame
    validate(a) {
      onlyKeys(a, ['index', 'indices']);
      const hasIndex = a.index != null, hasIndices = a.indices != null;
      if (hasIndex === hasIndices) fail('frame', 'exactly one of "index" / "indices" is required');
      if (hasIndex) {
        if (!Number.isInteger(a.index) || a.index < 0) fail('frame', '"index" must be an integer >= 0');
        return { op: 'frame', index: a.index };
      }
      if (!Array.isArray(a.indices) || !a.indices.length) fail('frame', '"indices" must be a non-empty array');
      if (a.indices.length > LIMITS.frameIndices) fail('frame', `more than ${LIMITS.frameIndices} indices`);
      for (const i of a.indices) if (!Number.isInteger(i) || i < 0) fail('frame', 'indices must be integers >= 0');
      return { op: 'frame', indices: a.indices.slice() };
    },
    run: async (a, { exportImage, loadFrame, results }) => {
      if (!loadFrame) throw new Error('The current input is not a video — the "frame" operation needs a video input');
      if (a.index != null) { await loadFrame(a.index); return; }
      // Multiple indices → one extra output image per frame (like variants).
      for (const idx of a.indices) {
        await loadFrame(idx);
        if (exportImage) results.push({ label: `frame${idx}`, dataUrl: await exportImage() });
      }
    },
  },

  // ── §2.1 multi-image ops: switch to a turn attachment / persist the result.
  // topLevelOnly shares the editor-settings enforcement (never inside variants or
  // ask previews) without being settings ops. ──
  image: {
    bullet: `- {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
  message (1-based, in attachment order); coordinates in later actions are in THAT
  image's pixel frame. Only valid when the user attached images. Use it to edit several
  attached images in one plan, giving each image its OWN actions.`,
    requires: ['loadAttachment'],
    topLevelOnly: true,
    newFrame: true,   // the attachment becomes the working image — a new coordinate frame
    validate(a) {
      onlyKeys(a, ['index']);
      if (!Number.isInteger(a.index) || a.index < 1) fail('image', '"index" must be an integer >= 1');
      return { op: 'image', index: a.index };
    },
    run: async (a, { loadAttachment, notes }) => {
      if (!loadAttachment) throw new Error('This surface cannot switch to attached images');
      // §2.1: an index this turn cannot satisfy fails the ACTION, not the plan.
      try { await loadAttachment(a.index); }
      catch (err) { notes?.push(`Skipped switching to attached image ${a.index} — ${err?.message || err}`); }
    },
  },
  save: {
    bullet: `- {"op":"save","name":"portrait 1"} — save the current image with its drawn lines as a
  project. When the user asks to process several images and keep the results, finish
  each image's actions with a "save" before switching to the next: image 1, its edits,
  save, image 2, its edits, save, …`,
    requires: ['saveProject'],
    topLevelOnly: true,
    validate(a) {
      onlyKeys(a, ['name', 'path']);
      const out = { op: 'save' };
      if (a.name != null) {
        if (!isStr(a.name, 120)) fail('save', '"name" must be a string of at most 120 characters');
        out.name = a.name;
      }
      // §2.1 "path": valid on every surface (desktop/cli shape: string ≤ 1024, no
      // URL scheme); the browser has no user filesystem, so the executor notes+skips it.
      if (a.path != null) {
        if (!isStr(a.path, LIMITS.pathChars)) fail('save', `"path" must be a string of at most ${LIMITS.pathChars} characters`);
        const path = a.path.trim();
        if (/^[a-z][a-z0-9+.-]*:\/\//i.test(path)) fail('save', '"path" is a local path, not a URL');
        if (path) out.path = path;
      }
      return out;
    },
    run: async (a, { stencil, saveProject, notes }) => {
      if (!saveProject) throw new Error('This surface cannot save projects');
      // §2.1: saving nothing is a skipped action, never a failed plan.
      if (!stencil.imageSize) { notes?.push('Skipped save — no working image to save'); return; }
      // A destination cannot be honoured here — browser saves are local projects.
      if (a.path) notes?.push('Saved to the usual place — this surface cannot save to a path');
      // An incognito editor cannot hold a saved project — the save IS the request to leave it,
      // so promote (the desktop's chatSaveProject does the same) rather than refusing.
      if (stencil.incognito) {
        stencil.promoteIncognito?.();
        notes?.push('Left incognito — saved as a local project');
        return;
      }
      await saveProject(a.name || '');
    },
  },

  // ── §10 editor-settings ops (browser & desktop editors only; forbidden in variants).
  // Same facade paths as the toolbar/settings UI: theme/accent use the flattened settings
  // accessors (not in apply()'s whitelist); the rest batch through stencil.apply. ──
  theme: {
    bullet: `- {"op":"theme","mode":"light"|"dark"} — switch the editor between light and dark
  ONLY; "mode" takes no other value. A COLOUR ("make the theme cyan") is the accent
  op below, never this one.`,
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['mode']);
      if (a.mode !== 'light' && a.mode !== 'dark') fail('theme', '"mode" must be "light" or "dark"');
      return { op: 'theme', mode: a.mode };
    },
    run: (a, { stencil }) => { stencil.darkTheme = a.mode === 'dark'; },
  },
  accent: {
    bullet: `- {"op":"accent","color":"#7c3aed"} — set the editor accent colour. "color" must be
  a #rrggbb hex, so translate colour names yourself (cyan = "#00ffff").`,
    also: `- "accent" also accepts {"op":"accent","preset":"green"} — a named preset persists and
  syncs; use a preset when the user names a colour that has one.`,
    alsoOrder: 3,
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['color', 'preset']);
      // §10: exactly one form — a raw hex (page-local custom accent) or a named
      // preset (persists and syncs). Unknown preset NAMES are a note + skip at run.
      const hasColor = a.color != null, hasPreset = a.preset != null;
      if (hasColor === hasPreset) fail('accent', 'exactly one of "color" / "preset" is required');
      if (hasColor) {
        if (!isStr(a.color) || !HEX.test(a.color)) fail('accent', '"color" must be #rrggbb');
        return { op: 'accent', color: a.color };
      }
      if (!isStr(a.preset, 40) || !a.preset.trim()) fail('accent', '"preset" must be a non-empty string');
      return { op: 'accent', preset: a.preset.trim().toLowerCase() };
    },
    run: (a, { stencil, notes }) => {
      if (a.color != null) { stencil.mainTheme = a.color; return; }
      // The facade routes a preset key to the persisting setAccent path and throws
      // on an unknown name — that throw is the §10 note, never a failed plan.
      try { stencil.mainTheme = a.preset; }
      catch (err) { notes?.push(`accent: ${err?.message || err}`); }
    },
  },
  lineStyle: {
    bullet: `- {"op":"lineStyle","color":"#00ff00","thickness":3,"pointSize":6,"style":"dashed"} —
  change the DEFAULT style for new lines (any subset of fields).`,
    also: `- "lineStyle" also carries "pointColor" ("" = follow the stroke), "drawMode"
  ("line"|"rect") and "fillColor" for the defaults of NEW lines.`,
    alsoOrder: 4,
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['color', 'pointColor', 'thickness', 'pointSize', 'style', 'drawMode', 'fillColor']);
      const out = { op: 'lineStyle' };
      if (a.color != null) {
        if (!isStr(a.color) || !(HEX.test(a.color) || CSS_NAME.test(a.color))) fail('lineStyle', '"color" must be #rrggbb or a CSS color name');
        out.color = a.color;
      }
      // §10: "" = follow the stroke colour (the point-colour control's clear).
      if (a.pointColor != null) {
        if (!isStr(a.pointColor) || (a.pointColor !== '' && !HEX.test(a.pointColor))) fail('lineStyle', '"pointColor" must be #rrggbb or "" (follow the stroke)');
        out.pointColor = a.pointColor;
      }
      if (a.thickness != null) {
        if (!Number.isInteger(a.thickness) || a.thickness < THICKNESS_MIN || a.thickness > THICKNESS_MAX) fail('lineStyle', `"thickness" must be an integer ${THICKNESS_MIN}..${THICKNESS_MAX}`);
        out.thickness = a.thickness;
      }
      if (a.pointSize != null) {
        if (!Number.isInteger(a.pointSize) || a.pointSize < POINT_SIZE_MIN || a.pointSize > POINT_SIZE_MAX) fail('lineStyle', `"pointSize" must be an integer ${POINT_SIZE_MIN}..${POINT_SIZE_MAX}`);
        out.pointSize = a.pointSize;
      }
      if (a.style != null) {
        if (!STYLES.has(a.style)) fail('lineStyle', `unknown line style "${a.style}"`);
        out.style = a.style;
      }
      if (a.drawMode != null) {
        if (a.drawMode !== 'line' && a.drawMode !== 'rect') fail('lineStyle', '"drawMode" must be "line" or "rect"');
        out.drawMode = a.drawMode;
      }
      if (a.fillColor != null) {
        if (!isStr(a.fillColor) || (a.fillColor !== 'transparent' && !HEX.test(a.fillColor))) fail('lineStyle', '"fillColor" must be #rrggbb or "transparent"');
        out.fillColor = a.fillColor;
      }
      if (Object.keys(out).length === 1) fail('lineStyle', 'needs at least one of color/pointColor/thickness/pointSize/style/drawMode/fillColor');
      return out;
    },
    run: (a, { stencil }) => {
      const opts = {};
      if (a.color != null) opts.lineColor = a.color;
      if (a.pointColor != null) opts.pointColor = a.pointColor;
      if (a.thickness != null) opts.thickness = a.thickness;
      if (a.pointSize != null) opts.pointSize = a.pointSize;
      if (a.style != null) opts.lineStyle = a.style;
      if (a.drawMode != null) opts.drawMode = a.drawMode;
      if (a.fillColor != null) opts.fillColor = a.fillColor;
      stencil.apply(opts);
    },
  },
  units: {
    bullet: '- {"op":"units","value":"cm"|"in"} — display units.',
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['value']);
      if (a.value !== 'cm' && a.value !== 'in') fail('units', '"value" must be "cm" or "in"');
      return { op: 'units', value: a.value };
    },
    run: (a, { stencil }) => { stencil.apply({ unit: a.value }); },
  },
  view: {
    bullet: '- {"op":"view","points":true,"lines":false} — show or hide points and lines.',
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['points', 'lines']);
      const out = { op: 'view' };
      if (a.points != null) { if (typeof a.points !== 'boolean') fail('view', '"points" must be a boolean'); out.points = a.points; }
      if (a.lines != null) { if (typeof a.lines !== 'boolean') fail('view', '"lines" must be a boolean'); out.lines = a.lines; }
      if (Object.keys(out).length === 1) fail('view', 'needs at least one of points/lines');
      return out;
    },
    run: (a, { stencil }) => {
      const opts = {};
      if (a.points != null) opts.showPoints = a.points;
      if (a.lines != null) opts.showLines = a.lines;
      stencil.apply(opts);
    },
  },
  clear: {
    bullet: `- {"op":"clear"} — REMOVE the working image and its lines, leaving the editor empty.
  This is what "remove/delete/clear the image" means. Never answer that with
  {"op":"blank"}: a blank REPLACES the picture with a white page, which is not a
  removal. Takes no fields.`,
    // Editor-scope: it drops the working image rather than editing one, so it is
    // banned inside variants like the rest of §10.
    editorSetting: true,
    newFrame: true,
    validate(a) {
      onlyKeys(a, []);
      return { op: 'clear' };
    },
    run: (_a, { stencil }) => { stencil.newEditor(); },
  },
  openUrl: {
    bullet: `- {"op":"openUrl","url":"https://…","incognito":false} — load an image (or video
  frame) from a URL into the editor; "incognito": true loads it into THIS editor
  switched to incognito (nothing is saved), never a second tab or window, so the
  rest of your plan keeps acting on it. ONLY a URL the user themselves wrote in
  this conversation — never introduce, complete, or rewrite one.`,
    // Editor-scope like the settings ops (never valid inside variants); it loads
    // a NEW working image rather than adjusting a setting, but the same rules
    // apply: GUI editors only, unknown elsewhere.
    editorSetting: true,
    newFrame: true,
    validate(a) {
      onlyKeys(a, ['url', 'incognito']);
      if (!isStr(a.url) || !/^https?:\/\/\S+$/i.test(a.url.trim())) fail('openUrl', '"url" must be an http(s) URL');
      if (a.incognito != null && typeof a.incognito !== 'boolean') fail('openUrl', '"incognito" must be a boolean');
      const out = { op: 'openUrl', url: a.url.trim() };
      if (a.incognito != null) out.incognito = a.incognito;
      return out;
    },
    run: async (a, { stencil, userText, openIncognito, notes }) => {
      // The model may only ECHO the user: the exact URL must appear in the user's
      // own messages this conversation (the `connect` stance — a plan can never
      // introduce a host, and page/injected content can't smuggle one in).
      const typed = typeof userText === 'function' ? userText() : '';
      if (!typed.includes(a.url)) {
        throw new Error(`openUrl blocked: "${a.url}" is not a URL you gave in this conversation`);
      }
      if (a.incognito) {
        if (!openIncognito) throw new Error('This surface cannot open an incognito editor');
        // Adopts incognito in THIS editor and awaits the load, so the actions after it
        // act on the fetched picture exactly as the plain branch does.
        await openIncognito(a.url);
        notes?.push(`Loaded ${a.url} into a fresh incognito editor`);
        return;
      }
      try {
        await stencil.load(a.url);
      } catch (err) {
        // A bare TypeError("Failed to fetch") explains nothing — say what the editor CAN load.
        throw new Error(`Could not load ${a.url} (${err.message}) — the editor only loads direct `
          + 'image/video URLs from hosts that allow cross-origin reads; to pick images off a web '
          + "page, use the extension's assistant on that tab");
      }
      notes?.push(`Loaded ${a.url} into the editor`);
    },
  },
  connect: {
    // One shared bullet documents connect AND disconnect — disconnect carries none.
    bullet: `- {"op":"connect","server":"..."} / {"op":"disconnect","server":"..."} — manage the
  user's collaboration-server connections. Only a server the user has already saved
  may be named — never invent or suggest a new address. These editor ops are not
  image edits and cannot appear inside "variants".`,
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['server']);
      if (!isStr(a.server) || !a.server.trim()) fail('connect', '"server" must be a non-empty string');
      return { op: 'connect', server: a.server };
    },
    run: async (a, { stencil, savedServers }) => {
      // Resolved entry = the SAVED { url, token } — auth is the stored connection's own.
      await stencil.connect(resolveServer(a.server, savedServers ? savedServers() : [], 'saved servers'));
    },
  },
  disconnect: {
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['server']);
      if (!isStr(a.server) || !a.server.trim()) fail('disconnect', '"server" must be a non-empty string');
      return { op: 'disconnect', server: a.server };
    },
    run: (a, { stencil }) => {
      stencil.disconnect(resolveServer(a.server, stencil.connections || [], 'connected servers'));
    },
  },
  copy: {
    bullet: `- {"op":"copy"} — copy the current rendered image to the system clipboard. This IS
  what "copy the result / copy to clipboard" means; never answer that it cannot be
  done. Takes no fields.`,
    also: `- "copy" also accepts {"op":"copy","what":"layout"} — the layout JSON instead of the
  image.`,
    alsoOrder: 2,
    // §10: works the editor's copy controls, never the image — variants-banned.
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['what']);
      if (a.what == null || a.what === 'image') return { op: 'copy' };
      if (a.what !== 'layout') fail('copy', '"what" must be "image" or "layout"');
      return { op: 'copy', what: 'layout' };
    },
    run: async (a, { stencil, copyRendered, copyLayoutRendered, notes }) => {
      // §10 what:"layout": the layout JSON instead of the image — needs drawn lines.
      if (a.what === 'layout') {
        if (!(stencil.lines || []).length) { notes?.push('Skipped copy — no drawn lines to copy'); return; }
        try { await (copyLayoutRendered ? copyLayoutRendered() : stencil.copyLayout()); }
        catch (err) { notes?.push(`Copy to clipboard failed — ${err?.message || err}`); }
        return;
      }
      // §10: copying nothing is a skipped action, never a failed plan.
      if (!stencil.imageSize) { notes?.push('Skipped copy — no working image to copy'); return; }
      // The toolbar's DATA-section copy path. `copyRendered` (injected by the chat surface)
      // exposes the write's real outcome so a blocked clipboard lands in the REPLY as a
      // warning; without it, the chainable facade path fires as before.
      try { await (copyRendered ? copyRendered() : stencil.copyImage()); }
      catch (err) { notes?.push(`Copy to clipboard failed — ${err?.message || err}`); }
    },
  },
  // §10 project management: both run the surface's EXISTING remove flows, incl. their
  // user confirmation — a declined confirm is a note, never a failed plan.
  removeProject: {
    bullet: `- {"op":"removeProject","name":"portrait 1"} — remove ONE saved local project by its
  name; the app asks the user to confirm before anything is deleted.`,
    also: `- "removeProject" also accepts {"op":"removeProject","current":true} — remove the
  project that is open right now (confirmed in-app).`,
    alsoOrder: 1,
    requires: ['removeProjectNamed'],
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['name', 'current']);
      // §10: exactly one form — a name, or current:true (the active project).
      const hasName = a.name != null, hasCurrent = a.current != null;
      if (hasName === hasCurrent) fail('removeProject', 'exactly one of "name" / "current" is required');
      if (hasCurrent) {
        if (a.current !== true) fail('removeProject', '"current" must be true');
        return { op: 'removeProject', current: true };
      }
      if (!isStr(a.name, 120) || !a.name.trim()) fail('removeProject', '"name" must be a non-empty string of at most 120 characters');
      return { op: 'removeProject', name: a.name.trim() };
    },
    run: async (a, { stencil, removeProjectNamed, clearWorkingImage, notes }) => {
      if (!removeProjectNamed) throw new Error('This surface cannot manage projects');
      let name = a.name;
      if (a.current) {
        // The active SAVED project's name (an incognito or unsaved temporary
        // session is not a removable project).
        const cur = stencil.current;
        name = cur && !cur.incognito ? cur.name : null;
        if (!name) {
          // §10: nothing saved but a picture IS open — "remove this project" means the
          // thing on screen, so fall back to the `clear` flow behind the same confirm.
          if (clearWorkingImage && (stencil.imageSize || stencil.lines?.length)) {
            const cleared = await clearWorkingImage();
            if (cleared) notes?.push(`removeProject: ${cleared}`);
            return;
          }
          notes?.push('removeProject: no active saved project to remove');
          return;
        }
      }
      const note = await removeProjectNamed(name);
      if (note) notes?.push(`removeProject: ${note}`);
    },
  },
  clearProjects: {
    bullet: `- {"op":"clearProjects"} — remove EVERY saved local project. This IS what "clear/
  delete my projects" means; the app asks the user to confirm first. Server-stored
  projects are never touched from chat. Takes no fields.`,
    requires: ['clearLocalProjects'],
    editorSetting: true,
    validate(a) {
      onlyKeys(a, []);
      return { op: 'clearProjects' };
    },
    run: async (_a, { clearLocalProjects, notes }) => {
      if (!clearLocalProjects) throw new Error('This surface cannot manage projects');
      const note = await clearLocalProjects();
      if (note) notes?.push(`clearProjects: ${note}`);
    },
  },
  // §10 compare: the original-vs-edit view control. View-only — the exported image
  // is unchanged, so this never counts as editing the picture.
  compare: {
    bullet: `- {"op":"compare","mode":"none"|"original"|"vertical"|"horizontal","split":0.5} — the
  comparison view: the original beside/over the edit ("vertical" = side-by-side split).
  View-only; the exported image is unchanged.`,
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['mode', 'split']);
      if (!COMPARE_MODES.has(a.mode)) fail('compare', '"mode" must be "none", "original", "vertical" or "horizontal"');
      const out = { op: 'compare', mode: a.mode };
      if (a.split != null) {
        if (a.mode !== 'vertical' && a.mode !== 'horizontal') fail('compare', '"split" only applies to the split modes');
        if (!Number.isFinite(a.split) || a.split < SPLIT_MIN || a.split > SPLIT_MAX) fail('compare', `"split" must be a number ${SPLIT_MIN}..${SPLIT_MAX}`);
        out.split = a.split;
      }
      return out;
    },
    run: (a, { stencil }) => {
      stencil.compareMode = a.mode;
      if (a.split != null) stencil.compareSplit = a.split;
    },
  },
  // §10 zoom: the user's VIEW only — cropping is the crop op.
  zoom: {
    bullet: `- {"op":"zoom","percent":150} or {"op":"zoom","fit":true} — zoom the USER'S VIEW (or
  fit to the window). This never changes the picture — cropping is the crop op.`,
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['percent', 'fit']);
      const hasPercent = a.percent != null, hasFit = a.fit != null;
      if (hasPercent === hasFit) fail('zoom', 'exactly one of "percent" / "fit" is required');
      if (hasFit) {
        if (a.fit !== true) fail('zoom', '"fit" must be true');
        return { op: 'zoom', fit: true };
      }
      if (!Number.isFinite(a.percent) || a.percent < ZOOM_MIN || a.percent > ZOOM_MAX) fail('zoom', `"percent" must be a number ${ZOOM_MIN}..${ZOOM_MAX}`);
      return { op: 'zoom', percent: a.percent };
    },
    run: (a, { stencil }) => {
      if (a.fit) stencil.zoomFit();
      else stencil.zoomLevel = a.percent;
    },
  },
  // §10 renameProject: the inline project-rename control; the store's own errors
  // (duplicate name, no active project) come back as notes, never failed plans.
  renameProject: {
    bullet: '- {"op":"renameProject","name":"…"} — rename the active saved project.',
    requires: ['renameActiveProject'],
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['name']);
      if (!isStr(a.name, 80) || !a.name.trim()) fail('renameProject', '"name" must be a non-empty string of at most 80 characters');
      return { op: 'renameProject', name: a.name.trim() };
    },
    run: async (a, { renameActiveProject, notes }) => {
      if (!renameActiveProject) throw new Error('This surface cannot manage projects');
      const note = await renameActiveProject(a.name);
      if (note) notes?.push(`renameProject: ${note}`);
    },
  },
  // §10 projectColor: the project name-colour control ("" restores the theme accent).
  projectColor: {
    bullet: `- {"op":"projectColor","color":"#ec4899"} — the project's name colour ("" = theme).`,
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['color']);
      if (!isStr(a.color) || (a.color !== '' && !HEX.test(a.color))) fail('projectColor', '"color" must be #rrggbb or "" (clear)');
      return { op: 'projectColor', color: a.color };
    },
    run: (a, { stencil, notes }) => {
      // The facade throws without an active project — a note, never a failed plan.
      try { stencil.projectColor = a.color; }
      catch (err) { notes?.push(`projectColor: ${err?.message || err}`); }
    },
  },
  // §10 blankColor: recolour a BLANK project's background, keeping every drawn line.
  // Valid only on a blank project — note + skip otherwise.
  blankColor: {
    bullet: `- {"op":"blankColor","color":"#dbeafe"} — recolour a BLANK project's background,
  KEEPING the drawn lines. "Recolour/change the background" means THIS, never a new
  {"op":"blank"} (that replaces the page and destroys the lines).`,
    requires: ['setBlankColor'],
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['color']);
      if (!isStr(a.color) || !(HEX.test(a.color) || CSS_NAME.test(a.color))) fail('blankColor', '"color" must be #rrggbb or a CSS color name');
      return { op: 'blankColor', color: a.color };
    },
    run: async (a, { setBlankColor, notes }) => {
      if (!setBlankColor) throw new Error('This surface cannot recolour blank projects');
      const note = await setBlankColor(a.color);
      if (note) notes?.push(`blankColor: ${note}`);
    },
  },
  // §10 openProject: the projects modal's open path — resolves like removeProject
  // (exact, else unique case-insensitive prefix); replacing an unsaved dirty editor
  // goes through the surface's own confirm inside the injected capability.
  openProject: {
    bullet: `- {"op":"openProject","name":"…"} — open a saved local project into the editor (the
  app confirms first when unsaved work would be replaced).`,
    requires: ['openProjectNamed'],
    editorSetting: true,
    newFrame: true,   // a switched project is a fresh working image — remapping resets
    validate(a) {
      onlyKeys(a, ['name']);
      if (!isStr(a.name, 120) || !a.name.trim()) fail('openProject', '"name" must be a non-empty string of at most 120 characters');
      return { op: 'openProject', name: a.name.trim() };
    },
    run: async (a, { openProjectNamed, notes }) => {
      if (!openProjectNamed) throw new Error('This surface cannot manage projects');
      const note = await openProjectNamed(a.name);
      if (note) notes?.push(`openProject: ${note}`);
    },
  },
  // §10 incognito: the toggle throws unless the editor is blank — note + skip.
  incognito: {
    bullet: '- {"op":"incognito","on":true} — edit without saving; only togglable on a blank editor.',
    editorSetting: true,
    validate(a) {
      onlyKeys(a, ['on']);
      if (typeof a.on !== 'boolean') fail('incognito', '"on" must be a boolean');
      return { op: 'incognito', on: a.on };
    },
    run: (a, { stencil, notes }) => {
      try { stencil.incognito = a.on; }
      catch (err) { notes?.push(`incognito: ${err?.message || err}`); }
    },
  },
  // §10 clearChat: the surface's clear-conversation flow behind the app's own confirm.
  // `deferred` runs it at the plan's END (a declined confirm costs nothing already done);
  // a caller `deferredSink` holds it past the §7 auto-continuation (chatController flushDeferred).
  clearChat: {
    bullet: `- {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
  confirm first, and the clear happens after this plan's other actions finish. This IS
  what "clear the chat / conversation / history" means; never answer that it cannot be
  done. Takes no fields.`,
    requires: ['clearChatConversation'],
    editorSetting: true,
    deferred: true,
    validate(a) {
      onlyKeys(a, []);
      return { op: 'clearChat' };
    },
    run: async (_a, { clearChatConversation, notes }) => {
      if (!clearChatConversation) throw new Error('This surface cannot clear the conversation');
      const note = await clearChatConversation();
      if (note) notes?.push(`clearChat: ${note}`);
    },
  },
};

// ── §13 registry-driven prompt assembly ─────────────────────────────────────

// Every capability the browser chat surface wires (chatSession.js), so the shipped prompt
// promises every registered op. assemblePrompts() with a reduced set drops bullets of ops
// whose capability is missing (§13 capability truth) — those fall to §1's unknown-op skip.
export const BROWSER_CAPABILITIES = new Set([
  'loadAttachment', 'saveProject', 'removeProjectNamed', 'clearLocalProjects',
  'renameActiveProject', 'setBlankColor', 'openProjectNamed', 'clearChatConversation',
]);

// §13 forbidden ops — the "never model-drivable" boundary, one name list per the
// contract's categories. Two enforcement teeth: a test that no OPS key uses one of
// these names, and the executor-level reject in executeOpPlan.
export const FORBIDDEN_OPS = new Set([
  'llm', 'provider', 'apiKey',   // llm/provider configuration
  'paste',                       // clipboard reads
  'hotkey', 'shortcut',          // hotkey rebinding
  'quit', 'exit',                // session/window end
  'chat',                        // chat persistence/consent TOGGLES (clearing is the clearChat op)
  'shareTabs',                   // sharing/server-side scope beyond what §10 grants
]);

// The typed error the executor throws for a forbidden op — even one that somehow
// bypassed the parser (which drops unknown names before they get here).
export class ForbiddenOpError extends Error {
  constructor(op) {
    super(`Forbidden operation "${op}" — this op is never model-drivable (contract §13)`);
    this.name = 'ForbiddenOpError';
    this.op = op;
  }
}

// §13 prompt censor: no generated bullet may read like provider/secret plumbing.
// \btoken\b (not a bare substring) so crop's legitimate "crop tokens" prose passes.
const CENSORED = /api\s*key|bearer|\btoken\b|base\s*url|endpoint/i;

// Assemble the §4 "Available ops" section and §10 settings block from the registry: entry
// order IS bullet order; "also accepts" lines follow the settings bullets (alsoOrder);
// entries whose `requires` are not all in `available` are excluded — never promised.
export const assemblePrompts = (available = BROWSER_CAPABILITIES) => {
  const coreBullets = [], settingsBullets = [], alsoEntries = [];
  for (const def of Object.values(OPS)) {
    if (def.requires && !def.requires.every((c) => available.has(c))) continue;
    if (def.bullet) (def.editorSetting ? settingsBullets : coreBullets).push(def.bullet);
    if (def.also) alsoEntries.push(def);
  }
  alsoEntries.sort((a, b) => a.alsoOrder - b.alsoOrder);
  const settings = [...settingsBullets, ...alsoEntries.map((d) => d.also)];
  for (const b of [...coreBullets, ...settings]) {
    if (CENSORED.test(b)) throw new Error(`Prompt censor: a registry bullet matches a sensitive pattern (api key/bearer/token/base url/endpoint): ${JSON.stringify(b.slice(0, 60))}`);
  }
  return {
    systemPrompt: PROMPT_CORE_HEAD + coreBullets.join('\n') + PROMPT_CORE_TAIL,
    settingsPrompt: settings.join('\n'),
  };
};

const ASSEMBLED = assemblePrompts();
// Canonical §4 prompt and the §10 editor-settings block, assembled at module load —
// clients may append a short dynamic suffix but never prepend anything.
export const LLM_SYSTEM_PROMPT = ASSEMBLED.systemPrompt;
export const EDITOR_SETTINGS_PROMPT = ASSEMBLED.settingsPrompt;

// The browser editor's full system prompt: §4 + the §10 block at the OP LIST's end — which
// is the `ask` paragraph's start, since §11 sits between the ops and the chat-only note.
// The anchor is verified: rewording §4 without updating it must fail loudly here, not
// silently ship an editor prompt with the §10 block missing.
const SETTINGS_SPLICE_ANCHOR = '\n\nWhen a choice is genuinely';
if (!LLM_SYSTEM_PROMPT.includes(SETTINGS_SPLICE_ANCHOR)) {
  throw new Error('LLM_SYSTEM_PROMPT no longer contains the editor-settings splice anchor');
}
export const EDITOR_SYSTEM_PROMPT = LLM_SYSTEM_PROMPT.replace(
  SETTINGS_SPLICE_ANCHOR,
  `\n${EDITOR_SETTINGS_PROMPT}${SETTINGS_SPLICE_ANCHOR}`);

// Derived from the registry: one appearing inside a variant fails the WHOLE plan.
const EDITOR_SETTINGS_OPS = new Set(Object.keys(OPS).filter((op) => OPS[op].editorSetting));
// §2.1 image/save ride the same top-level-only enforcement (the message differs).
const TOP_LEVEL_ONLY_OPS = new Set(Object.keys(OPS).filter((op) => OPS[op].editorSetting || OPS[op].topLevelOnly));

// §1: a misplaced op inside a variant / ask preview. Typed so callers can DROP that one
// variant (or that option's preview) with a warning instead of failing the whole plan.
export class MisplacedOpError extends Error {
  constructor(reason) {
    super(`Invalid plan: ${reason}`);
    this.name = 'MisplacedOpError';
    this.reason = reason;
  }
}

// Validate one actions list: unknown ops drop with a warning (forward compatibility);
// a known op with invalid params throws — nothing executes (contract §1). `scope`
// (non-null = inside a variant/preview) names where the op landed, for the message.
const validateActions = (list, warnings, where, scope = null) => {
  if (list == null) return [];
  if (!Array.isArray(list)) throw new Error(`Invalid plan: ${where} must be an array`);
  if (list.length > LIMITS.actions) throw new Error(`Invalid plan: more than ${LIMITS.actions} actions in ${where}`);
  const out = [];
  for (const a of list) {
    if (!isObj(a) || typeof a.op !== 'string') throw new Error(`Invalid plan: every action in ${where} must be an object with an "op"`);
    if (scope && TOP_LEVEL_ONLY_OPS.has(a.op)) {
      throw new MisplacedOpError(EDITOR_SETTINGS_OPS.has(a.op)
        ? `editor-settings op "${a.op}" is not allowed inside ${scope}`
          + (a.op === 'openUrl' ? " — open the URL as a top-level action; picking images off a web page is the extension assistant's job" : '')
        : a.op === 'undo' || a.op === 'redo'
          ? `"${a.op}" steps the live edit history — a top-level action only, not allowed inside variants or previews`
          : `"${a.op}" is a top-level action only (§2.1) — not allowed inside ${scope}`);
    }
    const def = OPS[a.op];
    if (!def) { warnings.push(`Skipped unknown operation "${a.op}"`); continue; }
    out.push(def.validate(a));
  }
  return out;
};

// Take the first balanced { … } object (string-aware) from the text, or null.
const firstJsonObject = (text) => {
  const start = text.indexOf('{');
  if (start < 0) return null;
  let depth = 0, inStr = false, esc = false;
  for (let i = start; i < text.length; i++) {
    const c = text[i];
    if (inStr) {
      if (esc) esc = false;
      else if (c === '\\') esc = true;
      else if (c === '"') inStr = false;
      continue;
    }
    if (c === '"') inStr = true;
    else if (c === '{') depth++;
    else if (c === '}') { depth--; if (depth === 0) return text.slice(start, i + 1); }
  }
  return null;
};

// ── §11 interactive replies (`ask`) ─────────────────────────────────────────
// A question put back to the user as a choice card. Validated as strictly as an action:
// a card nobody can answer (too few/many options, an option that is both a render AND a
// reference) is a plan error, not something to paper over.
const ASK_KEYS = new Set(['question', 'mode', 'options', 'allowCustom', 'customLabel']);
const OPTION_KEYS = new Set(['label', 'actions', 'image']);
const IMAGE_KEYS = ['url', 'projectId', 'scanIndex'];

// One option's `image` reference: EXACTLY one of url / projectId / scanIndex. The client
// resolves it (or renders the option pictureless); nothing here fetches anything.
const validateAskImage = (img, where) => {
  if (!isObj(img)) throw new Error(`Invalid plan: ${where} "image" must be an object`);
  const keys = Object.keys(img);
  const unknown = keys.filter((k) => !IMAGE_KEYS.includes(k));
  if (unknown.length) throw new Error(`Invalid plan: ${where} "image" has unknown field "${unknown[0]}"`);
  const given = IMAGE_KEYS.filter((k) => img[k] != null);
  if (given.length !== 1) throw new Error(`Invalid plan: ${where} "image" needs exactly one of ${IMAGE_KEYS.join(', ')}`);
  const [key] = given;
  if (key === 'scanIndex') {
    if (!Number.isInteger(img.scanIndex) || img.scanIndex < 0) throw new Error(`Invalid plan: ${where} "image.scanIndex" must be an integer >= 0`);
    return { scanIndex: img.scanIndex };
  }
  if (!isStr(img[key]) || !img[key].trim()) throw new Error(`Invalid plan: ${where} "image.${key}" must be a non-empty string`);
  // Only http(s) is fetchable — a data:/file:/javascript: "image" is never followed.
  if (key === 'url' && !/^https?:\/\//i.test(img.url.trim())) throw new Error(`Invalid plan: ${where} "image.url" must be an http(s) URL`);
  return { [key]: img[key].trim() };
};

const validateAskOption = (opt, i, warnings) => {
  const where = `ask option ${i + 1}`;
  if (!isObj(opt)) throw new Error(`Invalid plan: ${where} must be an object`);
  const unknown = Object.keys(opt).filter((k) => !OPTION_KEYS.has(k));
  if (unknown.length) throw new Error(`Invalid plan: ${where} has unknown field "${unknown[0]}"`);
  if (!isStr(opt.label) || !opt.label.trim()) throw new Error(`Invalid plan: ${where} "label" must be a non-empty string`);
  if (opt.label.length > ASK_LIMITS.label) throw new Error(`Invalid plan: ${where} "label" is longer than ${ASK_LIMITS.label} characters`);
  if (opt.actions != null && opt.image != null) throw new Error(`Invalid plan: ${where} carries both "actions" and "image" — an option previews a render OR names an existing image`);
  const out = { label: opt.label.trim() };
  // Preview actions are ordinary §2 actions, RENDERED never executed, so editor-settings
  // ops are treated as "inside a variant" (rejected). §1 leniency: a misplaced op costs
  // this option its PICTURE, not the plan — §11.2 already renders pictureless options.
  if (opt.actions != null) {
    try {
      out.actions = validateActions(opt.actions, warnings, where, 'variants or previews');
    } catch (err) {
      if (!(err instanceof MisplacedOpError)) throw err;
      warnings.push(`Dropped the preview for ${where} ("${out.label}") — ${err.reason}; the option is still offered`);
    }
  }
  if (opt.image != null) out.image = validateAskImage(opt.image, where);
  return out;
};

// Validate the optional `ask` object → the normalised card, or null when absent.
export const validateAsk = (ask, warnings) => {
  if (ask == null) return null;
  if (!isObj(ask)) throw new Error('Invalid plan: "ask" must be an object');
  const unknown = Object.keys(ask).filter((k) => !ASK_KEYS.has(k));
  if (unknown.length) throw new Error(`Invalid plan: "ask" has unknown field "${unknown[0]}"`);
  if (!isStr(ask.question) || !ask.question.trim()) throw new Error('Invalid plan: "ask.question" must be a non-empty string');
  if (ask.question.length > ASK_LIMITS.question) throw new Error(`Invalid plan: "ask.question" is longer than ${ASK_LIMITS.question} characters`);
  const mode = ask.mode == null ? 'single' : ask.mode;
  if (mode !== 'single' && mode !== 'multi') throw new Error('Invalid plan: "ask.mode" must be "single" or "multi"');
  if (!Array.isArray(ask.options)) throw new Error('Invalid plan: "ask.options" must be an array');
  if (ask.options.length < ASK_LIMITS.minOptions || ask.options.length > ASK_LIMITS.maxOptions)
    throw new Error(`Invalid plan: "ask.options" must hold ${ASK_LIMITS.minOptions}..${ASK_LIMITS.maxOptions} options`);
  if (ask.allowCustom != null && typeof ask.allowCustom !== 'boolean') throw new Error('Invalid plan: "ask.allowCustom" must be a boolean');
  if (ask.customLabel != null) {
    if (!isStr(ask.customLabel) || !ask.customLabel.trim()) throw new Error('Invalid plan: "ask.customLabel" must be a non-empty string');
    if (ask.customLabel.length > ASK_LIMITS.label) throw new Error(`Invalid plan: "ask.customLabel" is longer than ${ASK_LIMITS.label} characters`);
  }
  return {
    question: ask.question.trim(),
    mode,
    allowCustom: ask.allowCustom === true,
    customLabel: (ask.customLabel && ask.customLabel.trim()) || DEFAULT_CUSTOM_LABEL,
    options: ask.options.map((o, i) => validateAskOption(o, i, warnings)),
  };
};

// The text an answered card sends as the user's next turn: the picked labels joined, or the
// typed custom text. Trimmed and capped so a pasted essay can't ride back as one "answer".
export const askAnswerText = (ask, { picked = [], custom = '' } = {}) => {
  const typed = String(custom || '').trim();
  if (typed) return typed.slice(0, ASK_LIMITS.answer);
  const labels = (Array.isArray(picked) ? picked : [picked])
    .map((p) => (isObj(p) ? p.label : p)).filter((l) => isStr(l) && l.trim());
  return labels.join(', ').slice(0, ASK_LIMITS.answer);
};

// Raw LLM reply → validated plan { reply, actions, variants, warnings, chatOnly }.
// §1 extraction tolerance: fences stripped, first balanced JSON object wins; no JSON
// object at all = a chat-only turn (raw text = reply — not an error). Invalid plans THROW.
export const parseOpPlan = (text) => {
  const raw = String(text == null ? '' : text);
  const chatOnly = () => ({ reply: raw.trim(), actions: [], variants: [], ask: null, warnings: [], chatOnly: true });
  const candidate = firstJsonObject(raw.replace(/```[a-zA-Z]*/g, ''));
  if (candidate == null) return chatOnly();
  let obj;
  try { obj = JSON.parse(candidate); } catch { return chatOnly(); }   // not actually JSON → chat-only

  // `version` other than 1 (or absent) is accepted but ignored.
  const warnings = [];
  // §1 reply tolerance: models routinely omit the reply while planning valid
  // actions — substitute rather than lose the plan to a missing pleasantry.
  const replyOmitted = typeof obj.reply !== 'string' || !obj.reply.trim();
  let reply = replyOmitted ? '' : obj.reply;
  const actions = validateActions(obj.actions, warnings, '"actions"');
  if (obj.variants != null && !Array.isArray(obj.variants)) throw new Error('Invalid plan: "variants" must be an array');
  const rawVariants = obj.variants || [];
  if (rawVariants.length > LIMITS.variants) throw new Error(`Invalid plan: more than ${LIMITS.variants} variants`);
  // §1 leniency: a variant holding a top-level-only/settings op is DROPPED with a
  // warning naming it — the top-level actions and the well-formed variants still run.
  const variants = [];
  rawVariants.forEach((v, i) => {
    if (!isObj(v)) throw new Error('Invalid plan: every variant must be an object');
    if (v.label != null && !isStr(v.label)) throw new Error('Invalid plan: variant "label" must be a string');
    const label = v.label || `variant ${i + 1}`;
    try {
      variants.push({ label, actions: validateActions(v.actions, warnings, `variant ${i + 1}`, 'variants') });
    } catch (err) {
      if (!(err instanceof MisplacedOpError)) throw err;
      warnings.push(`Dropped variant ${i + 1} ("${label}") — ${err.reason}; the rest of the plan ran`);
    }
  });
  const ask = validateAsk(obj.ask, warnings);
  // The substitute must not overstate what happened: "Done." only when the plan
  // actually carries work — an empty plan says so, since a bare "Done." there
  // reads as a success that never occurred.
  if (replyOmitted) {
    if (actions.length || variants.length || ask) {
      reply = 'Done.';
      warnings.push('The model omitted its reply — the plan still ran');
    } else {
      reply = 'The model returned an empty plan — nothing was changed.';
    }
  }
  return { reply, actions, variants, ask, warnings, chatOnly: false };
};

// Variant labels name files/projects — keep them short and filesystem-safe.
export const sanitizeLabel = (label) => String(label == null ? '' : label)
  .trim().replace(/[^\w \-]+/g, '').replace(/\s+/g, ' ').slice(0, 40).trim() || 'variant';

// §10 connect/disconnect: resolve `server` against a list the USER owns — exact URL
// match, else a UNIQUE host match; anything else fails the plan. The model can NEVER
// introduce a new host, and plans never carry tokens. The matched ENTRY is returned
// (so a saved server's stored token rides along).
export const resolveServer = (server, entries, what) => {
  const want = String(server ?? '').trim();
  const urlOf = (e) => (typeof e === 'string' ? e : e.url);
  const exact = entries.find((e) => urlOf(e) === want);
  if (exact != null) return exact;
  const w = want.toLowerCase();
  const matches = entries.filter((e) => {
    try {
      const u = new URL(urlOf(e));
      return u.host.toLowerCase() === w || u.hostname.toLowerCase() === w;
    } catch { return false; }
  });
  if (matches.length === 1) return matches[0];
  throw new Error(matches.length
    ? `Unknown server "${server}" — that host matches several ${what}; use the full URL`
    : `Unknown server "${server}" — not among your ${what}`);
};

// ── Sandboxing variants / ask previews ──────────────────────────────────────
// Both run their ops against the LIVE editor and then put it back. `load()` restores
// pixels ONLY — it leaves the settings a variant-legal op touched (filter, page,
// formulas) and it CLEARS the user's lines — so the state has to be captured too.
const captureEditorState = (stencil) => {
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
      for (const k of LINE_KEYS) if (k !== 'points' && l[k] != null) out[k] = l[k];
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
const capturePixels = async (stencil, exportImage) => {
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
const restoreWorkingImage = async (stencil, pixels, state, actions) => {
  const reload = (actions || []).some((a) => NEEDS_RELOAD.has(a.op));
  if (reload) await stencil.load(pixels);
  await restoreEditorState(stencil, state, reload);
};

// Execute a parsed plan against the frozen window.stencil facade — every op routes
// through the same facade methods the toolbar/console use, never new editor logic.
// The options are the surface's injected capabilities (chatSession.js wires them all);
// an absent one makes its op error out or note+skip per §10/§2.1. savedServers is the
// ONLY pool `connect` may resolve against (§10; plans never carry tokens).
// Returns { results: [{ label, dataUrl }], warnings }.
export const executeOpPlan = async (plan, stencil, { exportImage, loadFrame, savedServers, userText, openIncognito, loadAttachment, saveProject, copyRendered, copyLayoutRendered, removeProjectNamed, clearWorkingImage, clearLocalProjects, renameActiveProject, setBlankColor, openProjectNamed, clearChatConversation, deferredSink } = {}) => {
  const warnings = (plan.warnings || []).slice();
  const results = [];

  // Dispatch through the registry — the parser only emits ops it holds. `notes` lets an
  // executor report what IT did (rendered with the reply); `frame` is the §1 re-mapping
  // accumulated from executed crops/rotates, reset to identity by newFrame ops.
  const ctx = { stencil, exportImage, loadFrame, savedServers, userText, openIncognito, loadAttachment, saveProject, copyRendered, copyLayoutRendered, removeProjectNamed, clearWorkingImage, clearLocalProjects, renameActiveProject, setBlankColor, openProjectNamed, clearChatConversation, results, notes: warnings, frame: identityFrame() };
  const run = async (a) => {
    // §13: a forbidden op is refused with a typed error even if a plan carrying
    // one reached the executor without passing the parser's unknown-op drop.
    if (FORBIDDEN_OPS.has(a.op)) throw new ForbiddenOpError(a.op);
    await OPS[a.op].run(a, ctx);
    if (OPS[a.op].newFrame) Object.assign(ctx.frame, identityFrame());
  };

  // §10 clearChat defers to the plan's END, wherever it rode in the plan. A caller
  // `deferredSink` takes the deferred actions UNEXECUTED instead: the turn runner replays
  // them after the §7 auto-continuation round, at the turn's true end (chatController).
  const deferred = deferredSink || [];
  for (const a of plan.actions) {
    if (OPS[a.op]?.deferred) deferred.push(a);
    else await run(a);
  }

  if (plan.variants.length) {
    if (!exportImage) throw new Error('Variants need an image export capability');
    // Variants render IMAGES, so they need a working image — but a plan whose
    // actions already ran (settings, openUrl, blank…) must not be thrown away
    // for that: skip the renders with a warning instead of failing the turn.
    if (!stencil.imageSize) {
      warnings.push(`Skipped ${plan.variants.length} variant${plan.variants.length === 1 ? '' : 's'} — variants render images and no image is loaded yet`);
    } else {
      // Branch each variant from the state AFTER the top-level actions: snapshot the
      // working image + editor state, apply the variant's ops, export, then restore both.
      const state = captureEditorState(stencil);
      const base = await capturePixels(stencil, exportImage);
      // Each variant branches from the post-actions state, so its coordinate
      // re-mapping restarts from the transform the top-level actions built up.
      const postActionsFrame = { ...ctx.frame };
      for (const v of plan.variants) {
        try {
          Object.assign(ctx.frame, postActionsFrame);
          for (const a of v.actions) await run(a);
          results.push({ label: sanitizeLabel(v.label), dataUrl: await exportImage() });
        } finally {
          await restoreWorkingImage(stencil, base, state, v.actions);
        }
      }
    }
  }

  if (!deferredSink) for (const a of deferred) await run(a);

  return { results, warnings };
};

// ── §11 preview rendering ───────────────────────────────────────────────────
// Render the preview for each `ask` option carrying `actions` — same save/restore dance
// as variants, so a preview SUGGESTS an edit, never performs one. Options with an `image`
// reference (or nothing) come back without a dataUrl for the surface to resolve. One
// option's render failing costs that option its picture (+ warning), never the card.
export const renderAskPreviews = async (ask, stencil, { exportImage, loadFrame } = {}) => {
  const previews = [];
  const warnings = [];
  const renderable = (ask?.options || []).map((o, i) => [o, i]).filter(([o]) => o.actions?.length);
  if (!renderable.length || typeof exportImage !== 'function') return { previews, warnings };
  // A preview is a RENDER of the working image — without one the card still
  // stands (labels only, §11.2), so this must never fail the turn.
  if (!stencil.imageSize) {
    warnings.push('Option previews need a working image — the choices are shown without pictures');
    return { previews, warnings };
  }
  const state = captureEditorState(stencil);
  const base = await capturePixels(stencil, exportImage);
  for (const [opt, index] of renderable) {
    try {
      await executeOpPlan({ actions: opt.actions, variants: [], warnings: [] }, stencil, { exportImage, loadFrame });
      previews.push({ index, label: opt.label, dataUrl: await exportImage() });
    } catch (err) {
      warnings.push(`Could not preview "${opt.label}" — ${err?.message || err}`);
    } finally {
      // Whatever happened, the working image AND the editor state go back.
      await restoreWorkingImage(stencil, base, state, opt.actions);
    }
  }
  return { previews, warnings };
};
