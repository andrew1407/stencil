// Runs a .stc against the live editor. The core lowers the language to an op stream; this
// maps each op onto the SAME facade calls the toolbar uses, so a scripted edit and a
// clicked one are one code path. Deliberately not routed through js/llm/: an op plan is
// model output and carries anti-abuse caps a user's own script must not inherit.
import { cropSpecOf, parseScript, resolveShape } from '../core/script.js';
import { timeoutSignal } from '../net/abortable.js';
import { notify } from '../utils.js';

export class ScriptError extends Error {
  constructor(message, { line = 0, col = 0 } = {}) {
    super(message);
    this.name = 'ScriptError';
    this.line = line;
    this.col = col;
  }
}

const firstError = (program) => program.diagnostics.find((d) => d.severity === 'error');

// A source the browser cannot open: it has no filesystem, so only a URL can be fetched.
const needsUrl = (op) =>
  new ScriptError(
    `@source needs a URL in the browser — '${op.strs[0]}' is a local path`,
    { line: op.line, col: op.col },
  );

const sizeOf = (stencil) => {
  const s = stencil.imageSize || {};
  return { width: s.width || 0, height: s.height || 0 };
};

const shapeOp = (op, { stencil }) => {
  const shape = resolveShape(op, sizeOf(stencil));
  if (!shape) throw new ScriptError('this shape resolves to nothing', { line: op.line, col: op.col });
  const [color, style, fillColor, pointColor] = op.strs;
  stencil.setLines([{
    points: shape.points,
    color,
    style,
    fillColor,
    pointColor,
    thickness: shape.thickness,
    pointSize: shape.pointSize,
    locked: op.kind === 'rect',
  }], { mode: 'combine' });
};

// Project names are unique, so a script run twice lands beside its first result instead of
// failing on the name it took — the desktop's uniqueLocalProjectName does the same.
const freeName = (stencil, wanted) => {
  if (!stencil.getProjectByName(wanted)) return wanted;
  for (let n = 2; n < 1000; n += 1) {
    const candidate = `${wanted} ${n}`;
    if (!stencil.getProjectByName(candidate)) return candidate;
  }
  return wanted;
};

const stepHistory = (op, step) => {
  const steps = Math.max(1, Math.round(op.nums[0] ?? 1));
  for (let i = 0; i < steps; i += 1) step();
};

/* One entry per op kind: the table IS the dispatch. `ctx` carries the facade, the layout
 * fetcher and the last source, so a later @frame can re-open it; an entry throws a
 * ScriptError to stop the run. The lowerer emits `undo` only — the ledger replays survivors. */
const OP_RUNNERS = Object.freeze({
  async open(op, ctx) {
    const [spec] = op.strs;
    const kind = ctx.program.blocks[op.block]?.kind;
    if (kind !== 'url') throw needsUrl(op);
    ctx.lastSource = spec;
    await ctx.stencil.load(spec, {});
  },
  async frame(op, ctx) {
    if (!ctx.lastSource) {
      throw new ScriptError('@frame needs a @source first', { line: op.line, col: op.col });
    }
    await ctx.stencil.load(ctx.lastSource, { frame: op.nums[0] ?? 0 });
  },
  crop(op, { stencil }) {
    stencil.crop(cropSpecOf(op));
  },
  filter(op, { stencil }) {
    const [mode, tint] = op.strs;
    if (mode === 'custom') stencil.apply({ filter: 'custom', filterColor: tint });
    else stencil.apply({ filter: mode });
  },
  line: shapeOp,
  rect: shapeOp,
  async layout(op, ctx) {
    const [src, mode] = op.strs;
    const data = await ctx.fetchLayout(src, op);
    ctx.stencil.applyLayout(data, { mode: mode === 'replace' ? 'replace' : 'combine' });
  },
  undo(op, { stencil }) {
    stepHistory(op, () => stencil.undo());
  },
  redo(op, { stencil }) {
    stepHistory(op, () => stencil.redo());
  },
// §3: a named @save renames the open project first. An incognito editor has no name to
// take and never persists, and a blank one has no project yet — both just save.
  async save(op, { stencil }) {
    const [name] = op.strs;
    const project = stencil.current;
    if (name && project && !project.incognito && project.name !== name) {
      project.name = freeName(stencil, name);
    }
    await stencil.save();
  },
});

const runOp = async (op, ctx) => {
  if (Object.hasOwn(OP_RUNNERS, op.kind)) await OP_RUNNERS[op.kind](op, ctx);
};

/* Runs `text` against the editor. Nothing executes when the script has an error; a failure
 * part-way leaves the edits already applied, and says which line stopped it. Returns the
 * number of ops that ran. */
export const runScript = async (text, stencil, { fetchLayout } = {}) => {
  const program = parseScript(String(text ?? ''));
  const bad = firstError(program);
  if (bad) {
    const msg = `Script error on line ${bad.line}: ${bad.message}`;
    notify(msg, 'fail');
    throw new ScriptError(bad.message, bad);
  }

  const ctx = {
    stencil,
    program,
    lastSource: '',
    // Through the same timeout every other fetcher uses: a stalled layout URL must not
    // leave the run — and the flyout it holds open — pending for ever.
    fetchLayout: fetchLayout ?? (async (src, op) => {
      if (!/^https?:\/\//i.test(src)) throw needsUrl({ ...op, strs: [src] });
      const res = await fetch(src, { signal: timeoutSignal() });
      if (!res.ok) throw new ScriptError(`could not load the layout '${src}'`, op);
      return res.json();
    }),
  };

  let ran = 0;
  for (const op of program.ops) {
    try {
      await runOp(op, ctx);
      ran += 1;
    } catch (err) {
      const line = err instanceof ScriptError ? err.line : op.line;
      notify(`Script failed at line ${line}: ${err.message}`, 'fail');
      throw err;
    }
  }

  const warnings = program.diagnostics.filter((d) => d.severity === 'warning');
  notify('Script executed successfully', 'ok');
  if (warnings.length > 0) notify(`Line ${warnings[0].line}: ${warnings[0].message}`, 'info');
  return ran;
};

// The UI layer may not name the facade (js/console owns it), so it runs a script through
// this instead of reaching for the global itself.
export const runScriptHere = (text, opts) => runScript(text, window.stencil, opts);
