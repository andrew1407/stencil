// Shared rig for the opPlan.test.js family: the plan builder, the accept/reject
// shorthands and the stateful facade stub the executor specs drive.
import assert from 'node:assert';
import { parseOpPlan } from '../../js/llm/opPlan.js';

export const plan = (over = {}) => JSON.stringify({ version: 1, reply: 'ok', actions: [], variants: [], ...over });

// §1 leniency: a top-level-only or editor-settings op inside a variant or an ask-option preview
// drops that one variant with a warning rather than failing the plan. Returns the plan.
export const dropsWithWarning = (raw, re) => {
  const p = parseOpPlan(raw);
  assert.ok(p.warnings.some((w) => re.test(w)),
    `no warning matching ${re} — got ${JSON.stringify(p.warnings)}`);
  return p;
};

export const ok = (action) => parseOpPlan(plan({ actions: [action] })).actions[0];
export const bad = (action) => assert.throws(() => parseOpPlan(plan({ actions: [action] })), /Invalid/);

// The stub MODELS state, not just calls: variants and previews run against the live editor and
// must put it back. `load()` clears the line list, exactly like a real image load.
export const makeStub = (state = {}) => {
  const calls = [];
  // Geometry model for the executor's §1 coordinate re-mapping: `full` is the rotated-original
  // dims, `cropRect` the rect stencilApi exposes; only the before/after origin delta is read.
  const size0 = 'imageSize' in state ? state.imageSize : { width: 640, height: 480 };
  let full = size0 ? { w: size0.width, h: size0.height } : null;
  const tok = (t, cur, len) => (t == null ? cur : (String(t).endsWith('%') ? (parseFloat(t) / 100) * len : parseFloat(t)));
  const turn = (clockwise) => {
    if (!full) return;
    const r = stub.cropRect;
    stub.cropRect = clockwise
      ? { x: full.h - (r.y + r.height), y: r.x, width: r.height, height: r.width }
      : { x: r.y, y: full.w - (r.x + r.width), width: r.height, height: r.width };
    full = { w: full.h, h: full.w };
    stub.imageSize = { width: stub.cropRect.width, height: stub.cropRect.height };
  };
  const stub = {
    imageSize: size0,
    cropRect: full ? { x: 0, y: 0, width: full.w, height: full.h } : null,
    connections: [],
    filter: 'none', filterColor: '#7c3aed', pageSize: 'A4',
    allowFormulas: false, formulaX: '', formulaY: '',
    showPoints: true, showLines: true,
    lines: [],
    ...state,
    crop(spec) {
      calls.push(['crop', spec]);
      if (!full) return stub;
      const r = stub.cropRect;
      const x1 = tok(spec.x1, r.x, full.w), x2 = tok(spec.x2, r.x + r.width, full.w);
      const y1 = tok(spec.y1, r.y, full.h), y2 = tok(spec.y2, r.y + r.height, full.h);
      stub.cropRect = { x: Math.min(x1, x2), y: Math.min(y1, y2), width: Math.abs(x2 - x1), height: Math.abs(y2 - y1) };
      stub.imageSize = { width: stub.cropRect.width, height: stub.cropRect.height };
      return stub;
    },
    rotateLeft() { calls.push(['rotateLeft']); turn(false); return stub; },
    rotateRight() { calls.push(['rotateRight']); turn(true); return stub; },
    apply(opts) {
      calls.push(['apply', opts]);
      for (const k of ['filter', 'filterColor', 'pageSize', 'allowFormulas', 'formulaX', 'formulaY', 'showPoints', 'showLines'])
        if (opts[k] != null) stub[k] = opts[k];
      if (opts.page != null) stub.pageSize = opts.page;
      return stub;
    },
    async blank(color, opts) { calls.push(opts === undefined ? ['blank', color] : ['blank', color, opts]); return stub; },
    async load(url) { calls.push(['load', url]); stub.lines = []; return stub; },
    newEditor() { calls.push(['newEditor']); stub.lines = []; stub.imageSize = undefined; return stub; },
    // The silent installer the restore uses — no prompt, no toast (stencilApi.js).
    setLines(lines, opts) { calls.push(['setLines', lines, opts]); stub.lines = lines || []; return stub; },
    async connect(entry) { calls.push(['connect', entry]); return stub; },
    disconnect(url) { calls.push(['disconnect', url]); return stub; },
    copyImage() { calls.push(['copyImage']); return stub; },
    copyLayout() { calls.push(['copyLayout']); return stub; },
    undo() { calls.push(['undo']); return stub; },
    redo() { calls.push(['redo']); return stub; },
    zoomFit() { calls.push(['zoomFit']); return stub; },
  };
  // The paste path — it prompts once lines exist, so no executor may route through it.
  Object.defineProperty(stub, 'layout', {
    set(v) { calls.push(['layout', v]); stub.lines = (v && v.lines) || []; },
  });
  Object.defineProperty(stub, 'darkTheme', { set(v) { calls.push(['darkTheme', v]); } });
  // Mimics the facade's mainTheme rule: a #rrggbb hex or a known preset key lands,
  // anything else throws — the accent op's unknown-preset note path needs the throw.
  Object.defineProperty(stub, 'mainTheme', { set(v) {
    if (!/^#[0-9a-f]{6}$/i.test(v) && !['violet', 'green', 'aqua'].includes(v)) {
      throw new Error(`Unknown theme "${v}". Use a hex like #ff5623, or one of: violet, green, aqua`);
    }
    calls.push(['mainTheme', v]);
  } });
  // §10 view/settings setters the new ops drive (recorded like darkTheme).
  Object.defineProperty(stub, 'zoomLevel', { set(v) { calls.push(['zoomLevel', v]); } });
  Object.defineProperty(stub, 'compareMode', { set(v) { calls.push(['compareMode', v]); } });
  Object.defineProperty(stub, 'compareSplit', { set(v) { calls.push(['compareSplit', v]); } });
  Object.defineProperty(stub, 'pageWidth', { set(v) { calls.push(['pageWidth', v]); } });
  Object.defineProperty(stub, 'pageHeight', { set(v) { calls.push(['pageHeight', v]); } });
  // Mimics the facade's projectColor rule: no active project → throw (the note path).
  Object.defineProperty(stub, 'projectColor', { set(v) {
    if (!stub.current) throw new Error('No active project to colour');
    calls.push(['projectColor', v]);
  } });
  // Mimics the facade's incognito rule: only togglable on a blank editor.
  Object.defineProperty(stub, 'incognito', { set(v) {
    if (v && stub.imageSize) throw new Error('Incognito can only be enabled on a blank editor (before an image is loaded)');
    calls.push(['incognitoSet', v]);
  } });
  return { stub, calls };
};

// §1 re-mapping: the setLines calls a layout op made, in order.
export const drawnBy = (calls) => calls.filter(([n, , opts]) => n === 'setLines' && !opts).map(([, lines]) => lines);

export const askPlan = (ask) => parseOpPlan(JSON.stringify({ version: 1, reply: 'ok', ask }));
export const askOf = (ask) => askPlan(ask).ask;
