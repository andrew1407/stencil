// The logo banner twin pins the rows the CLI prints (cli/src/app/logo/mark.zig), byte for
// byte, as core/tests/format/logoBanner.test.cpp does.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MARKS, logoBannerLayout, rasteriseMark, logoBannerRows, logoBanner } from '../../../js/core/format/logoBanner.js';

test('the S raster matches the CLI 14x6 and 10x5 marks', () => {
  assert.deepEqual(rasteriseMark('S', false), [
    '   ●───────●  ', ' ●╱           ', '  ╲●──────●   ', '           ╲● ', '            ╱ ', '  ●────────●  ']);
  assert.deepEqual(rasteriseMark('S', true), ['  ●─────● ', ' ●        ', '  ●────●  ', '        ● ', ' ●─────●  ']);
});

test('the P and M marks keep their stems straight', () => {
  assert.deepEqual(rasteriseMark('P', false), [
    '   ●──────●   ', '   │       ╲● ', '   ●──────●╱  ', '   │          ', '   │          ', '   ●          ']);
  assert.deepEqual(rasteriseMark('M', false), [
    '  ●        ●  ', '  │╲      ╱│  ', '  │ ╲╲  ╱╱ │  ', '  │   ●●   │  ', '  │        │  ', '  ●        ●  ']);
});

test('the boxed banner is the rounded panel around the mark', () => {
  const l = logoBannerLayout(false);
  assert.equal(l.panelWidth, 16);
  assert.equal(l.labelRow, 3);
  const rows = logoBannerRows('S', false);
  assert.equal(rows.length, 10);
  assert.equal(rows[0], '   ╭──────────────╮');
  assert.equal(rows[1], '  ╱                ╲');
  assert.equal(rows[2], '  │    ●───────●   │');
  assert.equal(rows[8], '  ╲                ╱');
  assert.equal(rows[9], '   ╰──────────────╯');
  const compact = logoBannerRows('S', true);
  assert.equal(compact.length, 7);
  assert.equal(compact[0], '    ╭────────────╮');
  assert.equal(compact[1], '    │   ●─────●  │');
  assert.equal(compact[6], '    ╰────────────╯');
  for (const row of rows.slice(1, -1)) assert.equal([...row].length, l.indent + l.panelWidth + 2);
});

test('an unknown mark has no rows', () => {
  assert.deepEqual(MARKS, ['S', 'P', 'M']);
  assert.deepEqual(logoBanner('Q', false, true), []);
});
