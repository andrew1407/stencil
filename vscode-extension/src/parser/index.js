// The parser copies' entry point: the composition browser/js/core/script.js performs when
// wasm is absent. parseScriptJS and the two dump wrappers are pinned to it, declaration for
// declaration, by tests/parserParity.test.js.
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

// No wasm here: the fallback body IS the parser.
export const parseScript = parseScriptJS;

// The wasm path carries the core's own dump; the fallback formats the same text here.
export const scriptDump = (program) => program.dump ?? dumpProgram(program);
export const scriptDiagnostics = (program) => dumpDiagnostics(program);
