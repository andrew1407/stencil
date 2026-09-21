// Lexing, parsing and argument grammar for .stc. Mirrors core/tests/script.test.cpp:
// the two engines must answer the same way, case for case.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript, resolveShape, cropSpecOf } from '../js/core/script.js';
import { isHexColorWord } from '../js/core/script/lexer.js';

const hasCode = (p, code) => p.diagnostics.some((d) => d.code === code);
const codesOf = (src) => parseScript(src).diagnostics.map((d) => d.code);

test("lexer: '#' opens a comment unless the token is a hex colour", () => {
  const p = parseScript('@source a.png:\n  @filter #ccc # tail\n');
  assert.equal(p.hasErrors, false);
  assert.equal(p.ops.length, 2);
  assert.equal(p.ops[1].strs[0], 'custom');
  assert.equal(p.ops[1].strs[1], '#ccc');
  assert.equal(isHexColorWord('#aabbccdd'), true);
  assert.equal(isHexColorWord('#ggg'), false);
  assert.equal(isHexColorWord('#ccccc'), false);
});

test('lexer: a URL keeps its scheme and does not open a block', () => {
  const p = parseScript('@source https://example.com/a.png:\n  @filter bw\n');
  assert.equal(p.hasErrors, false);
  assert.equal(p.blocks.length, 1);
  assert.equal(p.blocks[0].source, 'https://example.com/a.png');
  assert.equal(p.blocks[0].kind, 'url');
});

test("lexer: ';' separates statements like a newline", () => {
  const p = parseScript('@crop 25%;@filter bw;@save out.png');
  assert.equal(p.hasErrors, false);
  assert.equal(p.ops.length, 3);
});

test('directives and keywords ignore case; paths do not', () => {
  const p = parseScript('@SOURCE A.png:\n  @FiLtEr BW\n');
  assert.equal(p.hasErrors, false);
  assert.equal(p.blocks[0].source, 'A.png');
  assert.equal(p.ops[1].strs[0], 'bw');
});

test('an unknown directive suggests the nearest one', () => {
  const p = parseScript('@crp 10%\n');
  assert.ok(hasCode(p, 'E_UNKNOWN_DIRECTIVE'));
  assert.match(p.diagnostics[0].message, /@crop/);
});

test('crop: the key form, and 1/2/4 positional insets', () => {
  const keys = parseScript('@source a.png:\n  @crop x1=10% x2=-10% y1=2cm y2=-1in\n');
  assert.equal(keys.hasErrors, false);
  assert.equal(keys.ops[1].toks[0], '10%');
  assert.equal(keys.ops[1].toks[3], '-1in');

  const one = parseScript('@source a.png:\n  @crop 10%\n');
  assert.deepEqual(one.ops[1].toks.slice(0, 4), ['10%', '-10%', '10%', '-10%']);

  const two = parseScript('@source a.png:\n  @crop 10% 20cm\n');
  assert.equal(two.ops[1].toks[2], '20cm');
  assert.equal(two.ops[1].toks[3], '-20cm');

  // Four values read x1 y1 x2 y2, so the second lands on the y axis.
  const four = parseScript('@source a.png:\n  @crop 1% 2% 3% 4%\n');
  assert.deepEqual(four.ops[1].toks.slice(0, 4), ['1%', '3%', '2%', '4%']);
});

test('crop: mixing the two forms is an error, as is a 3-value inset', () => {
  assert.ok(codesOf('@source a.png:\n  @crop x1=10% 20cm\n').includes('E_CROP_MIXED_FORM'));
  assert.ok(codesOf('@source a.png:\n  @crop 1 2 3\n').includes('E_CROP_ARITY'));
});

test('units: a bare number takes the current @use unit', () => {
  const p = parseScript('@source a.png:\n  @use cm\n  @line (1,2) (3,4)\n');
  assert.equal(p.hasErrors, false);
  assert.equal(p.ops[1].toks[0], '1cm');
  assert.equal(p.ops[1].toks[3], '4cm');
});

test('points: a unit binds to a component or to the whole pair', () => {
  const p = parseScript('@source a.png:\n  @line (14px, 50%) (56, 90)cm\n');
  assert.equal(p.hasErrors, false);
  assert.deepEqual(p.ops[1].toks.slice(0, 4), ['14px', '50%', '56cm', '90cm']);
});

test('@use line groups apply in any order', () => {
  const a = parseScript('@source a.png:\n  @use line #cccccc dashed, fill aqua, point red 2px, 3px\n'
    + '  @line (1,1) (2,2)\n');
  const b = parseScript('@source a.png:\n  @use line 3px, point red 2px, dashed, fill aqua, #cccccc\n'
    + '  @line (1,1) (2,2)\n');
  assert.equal(a.hasErrors, false);
  assert.equal(b.hasErrors, false);
  assert.deepEqual(a.ops[1].strs, b.ops[1].strs);
  assert.deepEqual(a.ops[1].nums, b.ops[1].nums);
  assert.equal(a.ops[1].strs[0], '#cccccc');
  assert.equal(a.ops[1].strs[1], 'dashed');
  assert.equal(a.ops[1].nums[0], 3);
});

test('two stroke colours in one @use line is an error', () => {
  assert.ok(codesOf('@source a.png:\n  @use line red blue\n').includes('E_DUP_LINE_COLOR'));
});

test('a rect is a locked line; two corners become four points on resolve', () => {
  const p = parseScript('@source a.png:\n  @rect (10,10) (100,80)\n');
  assert.equal(p.hasErrors, false);
  assert.equal(p.ops[1].kind, 'rect');
  assert.equal(p.ops[1].nums[2], 1);
  const { points } = resolveShape(p.ops[1], { width: 200, height: 100 });
  assert.deepEqual(points, [
    { x: 10, y: 10 }, { x: 100, y: 10 }, { x: 100, y: 80 }, { x: 10, y: 80 },
  ]);
});

test('a negative united length measures from the far edge', () => {
  const p = parseScript('@source a.png:\n  @line (0,0) (-10%, -20%)\n');
  const { points } = resolveShape(p.ops[1], { width: 200, height: 100 });
  assert.equal(points[1].x, 180);
  assert.equal(points[1].y, 80);
});

test('crop resolves against the live image size', () => {
  const p = parseScript('@source a.png:\n  @crop 10%\n');
  assert.deepEqual(cropSpecOf(p.ops[1]), { x1: '10%', x2: '-10%', y1: '10%', y2: '-10%' });
});
