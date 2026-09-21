// The caps in js/core/types.js, each proved by the smallest input that trips it.
// Mirrors core/tests/scriptLimits.test.cpp: both engines must refuse the same way.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript } from '../js/core/script.js';
import { lexScript } from '../js/core/script/lexer.js';
import {
  MAX_BLOCKS, MAX_LINES, MAX_OPS, MAX_POINTS_PER_LINE, MAX_SOURCE_CHARS, MAX_TEMPLATES,
  MAX_TOKENS,
} from '../js/core/script/types.js';

// The one error code a capped script reports, or '' when it reports none.
const onlyErrorCode = (src) => {
  const codes = [...new Set(parseScript(src).diagnostics
    .filter((d) => d.severity === 'error').map((d) => d.code))];
  return codes.length > 1 ? '<several>' : (codes[0] ?? '');
};

const hasErrors = (src) => parseScript(src).diagnostics.some((d) => d.severity === 'error');
const numbered = (before, after, times) =>
  Array.from({ length: times }, (_, i) => before + i + after).join('');
const rep = (line, times) => `${line}\n`.repeat(times);

// A chain of `@stencil` definitions, each body ten calls of the one below it.
const templateChain = (leaf, names) => names
  .map((name, i) => `@stencil ${name}:\n${rep(`    ${i === 0 ? leaf : `@use stencil ${names[i - 1]}`}`, 10)}\n`)
  .join('');

test('a script past MAX_LINES is refused before anything is parsed', () => {
  assert.equal(onlyErrorCode('\n'.repeat(MAX_LINES + 1) + '@filter bw\n'), 'E_LIMIT_LINES');
});

test('a script past MAX_OPS is refused while it lowers', () => {
  assert.equal(onlyErrorCode('@filter bw\n'.repeat(MAX_OPS + 1)), 'E_LIMIT_OPS');
});

test('more than MAX_BLOCKS @source blocks is an error', () => {
  const src = numbered('@source a', '.png:\n  @filter bw\n', MAX_BLOCKS + 1);
  assert.equal(onlyErrorCode(src), 'E_LIMIT_BLOCKS');
});

test('more than MAX_TEMPLATES @stencil definitions is an error', () => {
  const src = numbered('@stencil t', ':\n  @filter bw\n', MAX_TEMPLATES + 1);
  assert.equal(onlyErrorCode(src), 'E_LIMIT_TEMPLATES');
});

test('a line past MAX_POINTS_PER_LINE is an error', () => {
  const points = Array.from({ length: MAX_POINTS_PER_LINE + 1 }, (_, i) => `(${i},${i})`);
  assert.equal(onlyErrorCode(`@line ${points.join(' ')}\n`), 'E_LIMIT_POINTS');
});

test('a @source spec past MAX_SOURCE_CHARS is an error', () => {
  const src = `@source ${'a'.repeat(MAX_SOURCE_CHARS + 1)}.png:\n  @filter bw\n`;
  assert.equal(onlyErrorCode(src), 'E_LIMIT_SOURCE');
});

test('the replay an @undo/@save cycle emits counts against MAX_OPS', () => {
  // Every edit before the undone one replays at the @save, so the stream doubles.
  const cycle = `${rep('@filter bw', MAX_OPS / 2 + 100)}@undo 1\n@save\n`;
  const diags = parseScript(cycle).diagnostics;
  assert.deepEqual(diags.map((d) => d.code), ['E_LIMIT_OPS']);
  assert.equal(diags[0].severity, 'error');
  assert.equal(diags[0].message, 'the script has too many ops');
  assert.equal(hasErrors(`${rep('@filter bw', 100)}@undo 1\n@save\n`), false);
});

test('a template fan-out past MAX_OPS statements is an error', () => {
  // Six thousand statements from forty lines: the cap has to bite before they exist.
  const src = `${templateChain('@use px', ['ten', 'hundred', 'thousand'])}`
    + `@source a.png:\n${rep('    @use stencil thousand', 6)}`;
  assert.equal(onlyErrorCode(src), 'E_LIMIT_OPS');
});

test('a fan-out that produces no statement at all is bounded too', () => {
  // Bodies of nothing but nested uses, bottoming out in an empty one: no statement ever
  // lands, so neither the depth cap nor the op cap can stop it — only the expansion count.
  let src = '@stencil t8:\n\n';
  for (let k = 7; k >= 1; k -= 1) src += `@stencil t${k}:\n${rep(`    @use stencil t${k + 1}`, 5)}\n`;
  const program = parseScript(`${src}@use stencil t1\n`);
  assert.equal(onlyErrorCode(`${src}@use stencil t1\n`), 'E_LIMIT_OPS');
  assert.deepEqual(program.blocks, []); // a capped block is not recorded, so nothing dumps
});

test('a chain MAX_TEMPLATE_DEPTH deep that does produce ops is never refused', () => {
  // The expansion count a legitimate script reaches is its ops times its nesting depth,
  // which is what MAX_TEMPLATE_EXPANSIONS is sized from: this one sits far inside it.
  let src = '@stencil t16:\n  @filter bw\n\n';
  for (let k = 15; k >= 1; k -= 1) src += `@stencil t${k}:\n  @use stencil t${k + 1}\n\n`;
  const program = parseScript(`${src}${rep('@use stencil t1', 2000)}`);
  assert.equal(program.diagnostics.filter((d) => d.severity === 'error').length, 0);
  assert.equal(program.ops.length, 2000);
});

test('a script past MAX_TOKENS stops lexing and says so', () => {
  const { tokens, diagnostics } = lexScript('a '.repeat(MAX_TOKENS + 10));
  assert.equal(tokens.length, MAX_TOKENS);
  assert.deepEqual(diagnostics.map((d) => [d.severity, d.code, d.message]),
    [['error', 'E_LIMIT_TOKENS', `script has too many tokens (over ${MAX_TOKENS})`]]);
  assert.equal(diagnostics[0].len, 0);
});

test('a script right at each cap is accepted', () => {
  // Exactly MAX_LINES lines: the last one carries no newline of its own.
  assert.equal(hasErrors('\n'.repeat(MAX_LINES - 1) + '@filter bw'), false);
  assert.equal(hasErrors('@filter bw\n'.repeat(MAX_OPS)), false);
  assert.equal(hasErrors(numbered('@source a', '.png:\n  @filter bw\n', MAX_BLOCKS)), false);
});
