// The .stc entry point. Port of core/script/scriptProgram.cpp; the wasm build runs the C++
// and this body is the fallback, which browser/tests/wasm-parity-script.test.js pins op-for-op.
import { core } from './abi/stencilCore.js';
import { resolveAxisPx } from './settings/units.js';
import { hasErrors } from './script/diagnostics.js';
import { dumpDiagnostics, dumpProgram } from './script/dump.js';
import { lexScript } from './script/lexer.js';
import { lowerScript } from './script/lower.js';
import { parseScript as parseStatements } from './script/parser.js';

/* Parses a script into { tokens, diagnostics, blocks, ops }. Diagnostics come in source
 * order; a program with any error must not be executed. */
const parseScriptJS = (text) => {
  const lexed = lexScript(text);
  const parsed = parseStatements(lexed.tokens);
  parsed.diagnostics = [...lexed.diagnostics, ...parsed.diagnostics];

  const lowered = lowerScript(parsed);
  // Report in source order; lexing, parsing and lowering each find their own.
  const diagnostics = lowered.diagnostics
    .map((d, i) => [d, i])
    .sort((a, b) => (a[0].line - b[0].line) || (a[0].col - b[0].col) || (a[1] - b[1]))
    .map(([d]) => d);

  return {
    tokens: lexed.tokens,
    diagnostics,
    blocks: lowered.blocks,
    ops: lowered.ops,
    errorCount: diagnostics.reduce((n, d) => n + (d.severity === 'error' ? 1 : 0), 0),
    hasErrors: hasErrors(diagnostics),
  };
};

/* wasm when it is loaded, this body otherwise; the two agree fixture for fixture. The last
 * result is memoised, because the editors re-parse the same text several times per keystroke:
 * the program handed back is shared, so read it and never mutate it. */
let memo = { text: null, impl: null, program: null };

export const parseScript = (text) => {
  const src = String(text ?? '');
  const impl = core.op('scriptParse') ?? parseScriptJS;
  if (memo.impl !== impl || memo.text !== src) memo = { text: src, impl, program: impl(src) };
  return memo.program;
};

// The wasm path carries the core's own dump; the fallback formats the same text here.
export const scriptDump = (program) => program.dump ?? dumpProgram(program);
export const scriptDiagnostics = (program) => dumpDiagnostics(program);

const PX_PER_CM = 96 / 2.54;

/* Length tokens -> pixels against the CURRENT image size, because a crop changes it
 * mid-script; null for an op with no geometry, a two-point rect expanded to four corners.
 * CROP keeps its tokens: `stencil.crop` already owns edge resolution for the whole app. */
export const resolveShape = (op, { width, height, pxPerCm = PX_PER_CM }) => {
  if (op.kind !== 'line' && op.kind !== 'rect') return null;
  const xs = [];
  const ys = [];
  for (let i = 0; i + 1 < op.toks.length; i += 2) {
    const x = resolveAxisPx(op.toks[i], { lengthPx: width, pxPerCm });
    const y = resolveAxisPx(op.toks[i + 1], { lengthPx: height, pxPerCm });
    if (x === null || y === null) return null;
    xs.push(x);
    ys.push(y);
  }
  if (op.kind === 'rect' && xs.length === 2) { // two opposite corners close into a rectangle
    const [x0, x1] = xs;
    const [y0, y1] = ys;
    xs.splice(0, 2, x0, x1, x1, x0);
    ys.splice(0, 2, y0, y0, y1, y1);
  }
  return {
    points: xs.map((x, i) => ({ x, y: ys[i] })),
    thickness: op.nums[0] ?? 2,
    pointSize: op.nums[1] ?? 4,
  };
};

// The crop edges as a spec object `stencil.crop` accepts; '' means "leave this edge".
export const cropSpecOf = (op) => {
  const [x1, x2, y1, y2] = op.toks;
  const spec = {};
  if (x1) spec.x1 = x1;
  if (x2) spec.x2 = x2;
  if (y1) spec.y1 = y1;
  if (y2) spec.y2 = y2;
  if (op.strs[0]) spec.aspect = op.strs[0];
  if (op.nums[0]) spec.album = true;
  return spec;
};
