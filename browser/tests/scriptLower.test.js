// Templates, history and source classification. Mirrors core/tests/scriptLower.test.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript } from '../js/core/script.js';
import { editDistance, didYouMean } from '../js/core/script/scriptDiagnostics.js';
import { MAX_LINES, MAX_OPS, MAX_POINTS_PER_LINE, MAX_TEMPLATE_DEPTH } from '../js/core/script/scriptTypes.js';

const codesOf = (src) => parseScript(src).diagnostics.map((d) => d.code);
const kindOf = (src) => parseScript(src).blocks[0].kind;

test('templates: longest defined name wins, parameters fill positionally', () => {
  const p = parseScript('@stencil style:\n  @use line red\n\n@stencil style bold:\n'
    + '  @use line blue, 5px\n\n@source a.png:\n  @use stencil style bold:\n  @line (1,1) (2,2)\n');
  assert.equal(p.ops[1].strs[0], 'blue');
  assert.equal(p.ops[1].nums[0], 5);

  const q = parseScript('@stencil s p:\n  @use line @1, @2\n\n'
    + '@source a.png:\n  @use stencil s p red cm:\n  @line (1,1) (2,2)\n');
  assert.equal(q.hasErrors, false);
  assert.equal(q.ops[1].strs[0], 'red');
  assert.equal(q.ops[1].toks[0], '1cm');
});

test('templates: wrong arity, unknown name and no use are all reported', () => {
  assert.ok(codesOf('@stencil two p:\n  @use line @1, @2\n\n@source a.png:\n'
    + '  @use stencil two p red:\n').includes('E_TEMPLATE_ARITY'));
  assert.ok(codesOf('@source a.png:\n  @use stencil nowhere:\n').includes('E_UNDEFINED_TEMPLATE'));
  assert.ok(codesOf('@stencil unused:\n  @filter bw\n\n@source a.png:\n  @filter sepia\n')
    .includes('W_UNUSED_TEMPLATE'));
});

test('a template that reaches itself stops at the depth cap', () => {
  const src = '@stencil loop:\n  @use stencil loop:\n\n@source a.png:\n  @use stencil loop:\n';
  assert.ok(codesOf(src).includes('E_TEMPLATE_RECURSION'));
});

test('undo rewinds and replays so each save sees the right state', () => {
  const p = parseScript('@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n'
    + '  @save one.png\n  @undo\n  @save two.png\n  @redo\n  @save three.png\n');
  assert.equal(p.hasErrors, false);
  // One rewind before the second save, and the rect replayed after @redo.
  assert.equal(p.ops.filter((o) => o.kind === 'undo').length, 1);
  assert.equal(p.ops.filter((o) => o.kind === 'rect').length, 2);
});

test('undoing the first edit rewinds past every later one and replays them', () => {
  const p = parseScript('@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n'
    + '  @crop 10%\n  @undo 1\n  @save out.png\n');
  assert.equal(p.hasErrors, false);
  const undo = p.ops.filter((o) => o.kind === 'undo').at(-1);
  assert.ok(undo);
  assert.equal(undo.nums[0], 3);
});

test('undo selectors: by index, from the end, and by text', () => {
  const base = '@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n';
  assert.equal(parseScript(`${base}  @undo -1\n`).hasErrors, false);
  assert.equal(parseScript(`${base}  @undo @rect (1,1) (2,2)\n`).hasErrors, false);
  assert.ok(codesOf('@source a.png:\n  @filter bw\n  @undo 99\n').includes('E_UNDO_OUT_OF_RANGE'));
  assert.ok(codesOf('@source a.png:\n  @filter bw\n  @redo\n').includes('W_NOTHING_TO_REDO'));
});

test('@frame starts a fresh set of edits and rejects a repeat', () => {
  const p = parseScript('@source clip.mp4:\n  @frame 8\n  @filter bw\n  @save\n  @frame 90\n  @save\n');
  assert.equal(p.hasErrors, false);
  assert.ok(codesOf('@source clip.mp4:\n  @frame 8\n  @frame 8\n').includes('E_DUPLICATE_FRAME'));
  assert.ok(codesOf('@frame 3\n').includes('E_FRAME_OUTSIDE_SOURCE'));
});

test('a source spec is classified for the adapter that opens it', () => {
  assert.equal(kindOf('@source a.png:\n  @filter bw\n'), 'file');
  assert.equal(kindOf('@source shots/:\n  @filter bw\n'), 'dir');
  assert.equal(kindOf('@source shots/a*.png:\n  @filter bw\n'), 'glob');
  assert.equal(kindOf('@filter bw\n'), 'project');
});

test('a block body ends where its indentation does', () => {
  const p = parseScript('@stencil s:\n  @filter bw\n\n@rect (1,1) (2,2)\n@save\n');
  assert.equal(p.blocks.length, 1);
  assert.equal(p.blocks[0].kind, 'project');
  assert.equal(p.ops.length, 2);  // the rect and the save, not the template body
});

test('malformed input yields a diagnostic, never a crash', () => {
  const bad = ['@', '@@', '@source', '@source :', '@line (', '@line (1,', '@crop x1=',
    '@use line', '@stencil', '"', '@filter', '@undo @', '@line ()', '@@@@@'];
  for (const src of bad) assert.ok(parseScript(src).ops.length <= 1, src);
});

test('the caps are the same numbers the C++ port uses', () => {
  assert.equal(MAX_LINES, 20000);
  assert.equal(MAX_OPS, 5000);
  assert.equal(MAX_TEMPLATE_DEPTH, 16);
  assert.equal(MAX_POINTS_PER_LINE, 200);
});

test('editDistance is capped, and didYouMean only suggests close words', () => {
  assert.equal(editDistance('crop', 'crp'), 1);
  assert.equal(editDistance('crop', 'zzzzzzzz'), 3);
  assert.equal(didYouMean('crp', ['crop', 'filter']), 'crop');
  assert.equal(didYouMean('zzzzzzzz', ['crop', 'filter']), '');
});
