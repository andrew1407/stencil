// The caps in js/core/scriptTypes.js, each proved by the smallest input that trips it.
// Mirrors core/tests/scriptLimits.test.cpp: both engines must refuse the same way.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript } from '../js/core/script.js';
import {
  MAX_BLOCKS, MAX_LINES, MAX_OPS, MAX_POINTS_PER_LINE, MAX_SOURCE_CHARS, MAX_TEMPLATES,
} from '../js/core/scriptTypes.js';

// The one error code a capped script reports, or '' when it reports none.
const onlyErrorCode = (src) => {
  const codes = [...new Set(parseScript(src).diagnostics
    .filter((d) => d.severity === 'error').map((d) => d.code))];
  return codes.length > 1 ? '<several>' : (codes[0] ?? '');
};

const hasErrors = (src) => parseScript(src).diagnostics.some((d) => d.severity === 'error');
const numbered = (before, after, times) =>
  Array.from({ length: times }, (_, i) => before + i + after).join('');

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

test('a script right at each cap is accepted', () => {
  // Exactly MAX_LINES lines: the last one carries no newline of its own.
  assert.equal(hasErrors('\n'.repeat(MAX_LINES - 1) + '@filter bw'), false);
  assert.equal(hasErrors('@filter bw\n'.repeat(MAX_OPS)), false);
  assert.equal(hasErrors(numbered('@source a', '.png:\n  @filter bw\n', MAX_BLOCKS)), false);
});
