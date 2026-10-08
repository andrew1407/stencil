// The swatches a colour drag reads and writes (js/ui/drag/colorDragSwatches.js): which colour a target
// takes, a field and its opacity box as one colour, a well, a hidden picker, a registered control and a
// Lines-tab row, each applied through its own change path.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installMenuDom } from '../../helpers/menuDom.js';
import {
  colorToApply, parseSwatchColor, swatchOf, liveSwatches, registerColorSwatch, bindSwatchApp, ROW_SWATCHES,
} from '../../../js/ui/drag/colorDragSwatches.js';

const fake = (color, alpha) => ({ read: () => color, alpha });

test('both carrying alpha, the target takes the full RGBA; otherwise the RGB over its own alpha', () => {
  assert.equal(colorToApply(fake('#ff000080', true), fake('#00ff00', true)), '#ff000080');
  assert.equal(colorToApply(fake('#ff000080', true), fake('#00ff00', false)), '#ff0000');
  assert.equal(colorToApply(fake('#FF0000', false), fake('#00ff0040', true)), '#ff000040');
  assert.equal(colorToApply(fake('#ff0000', true), fake('#00ff0040', true)), '#ff0000',
    'an opaque source makes a translucent target opaque');
  assert.equal(colorToApply(fake('#f00', false), fake('#00ff00', false)), '#ff0000');
});

test('nothing to apply when the target already shows it or the source has no colour', () => {
  assert.equal(colorToApply(fake('#ff0000', false), fake('#FF0000', false)), null);
  assert.equal(colorToApply(fake('#ff000080', true), fake('#ff000080', true)), null);
  assert.equal(colorToApply(fake('#ff0000', false), fake('#ff000080', true)), null, 'the target keeps its alpha');
  assert.notEqual(colorToApply(fake('#ff0000', true), fake('#ff000080', true)), null);
  assert.equal(colorToApply(fake('transparent', true), fake('#00ff00', true)), null);
  assert.equal(colorToApply(fake('#ff000000', true), fake('#00ff00', true)), null, 'alpha 0 shows no colour to hand over');
  assert.equal(colorToApply(fake('#ff0000', false), fake('#ff000000', true)), '#ff000000',
    'a target at alpha 0 shows nothing, so even its own hex lands (a fill then lifts off zero)');
  assert.equal(parseSwatchColor(null), null);
});

const page = (markup) => {
  const doc = installMenuDom();
  doc.body.innerHTML = markup;
  return doc;
};
const seen = (el) => {
  const log = [];
  for (const t of ['input', 'change']) el.addEventListener(t, () => log.push(`${t}:${el.value}`));
  return log;
};

test('a field and its opacity box are one colour; a drop sets the box, then fires input + change once', (t) => {
  const doc = page(`<div class="control-group"><input type="color" id="sel-color" value="#112233">
    <input type="number" class="alpha-input" id="sel-alpha" value="128"></div>`);
  t.after(() => doc.restore());
  const field = doc.getElementById('sel-color');
  const s = swatchOf(field);
  assert.equal(s.el, field);
  assert.equal(s.alpha, true);
  assert.equal(s.read(), '#11223380');
  const log = seen(field);
  s.apply('#ff000040');
  assert.equal(doc.getElementById('sel-alpha').value, '64');
  assert.deepEqual(log, ['input:#ff0000', 'change:#ff0000']);
});

test('a lone field is RGB only; a hidden picker field is no swatch; a well is the label around its field', (t) => {
  const doc = page(`<input type="color" id="line-color" value="#abcdef">
    <input type="color" id="picker" aria-hidden="true" value="#000000">
    <label class="vs-ctrl vs-color" id="well"><input type="color" id="vs-fill" value="#010203"><span class="vs-hex">#010203</span></label>`);
  t.after(() => doc.restore());
  const lone = swatchOf(doc.getElementById('line-color'));
  assert.equal(lone.alpha, false);
  assert.equal(lone.read(), '#abcdef');
  assert.equal(swatchOf(doc.getElementById('picker')), null);
  const hex = doc.getElementById('well').querySelector('.vs-hex');
  const well = swatchOf(hex);
  assert.equal(well.el, doc.getElementById('well'), 'the hex text lifts the well too');
  assert.equal(well.read(), '#010203');
  doc.getElementById('vs-fill').disabled = true;
  assert.equal(well.enabled(), false);
});

test('a registered control answers for itself and its children', (t) => {
  const doc = page('<button id="blank-color-btn"><span class="blank-color-swatch"></span>Blank</button>');
  t.after(() => doc.restore());
  const btn = doc.getElementById('blank-color-btn');
  const applied = [];
  registerColorSwatch(btn, { read: () => '#ffffff', apply: (c) => applied.push(c) });
  const s = swatchOf(btn.querySelector('.blank-color-swatch'));
  assert.equal(s.el, btn);
  assert.equal(s.alpha, false);
  s.apply('#123456');
  assert.deepEqual(applied, ['#123456']);
});

const linesApp = (calls) => ({
  lines: [{ color: '#ff000080', pointColor: '', points: [] }, { color: '', pointColor: '#00ff00', points: [] }],
  selectedLines: [], selectedLineIdx: -1, readOnly: false,
  compareReadOnly() { return this.readOnly; },
  showSelectionPanel: (line) => calls.push(['panel', line.color]),
  coordTable: { update: () => {} },
  renderer: { redraw: () => {} },
  saveHistory: () => calls.push(['save']),
});

test('a Lines-tab row swatch reads its line and lands as its pick: the stroke selects, each is one step', (t) => {
  const doc = page(`<table><tbody><tr class="lines-row" data-idx="0"><td><span class="lines-swatch" id="s0"></span></td></tr>
    <tr class="lines-row" data-idx="1"><td><span class="lines-point-swatch" id="p1"></span></td></tr></tbody></table>`);
  t.after(() => { bindSwatchApp(null); doc.restore(); });
  const calls = [];
  const app = linesApp(calls);
  bindSwatchApp(app);
  const stroke = swatchOf(doc.getElementById('s0'));
  assert.equal(stroke.read(), '#ff000080');
  assert.equal(stroke.alpha, true);
  assert.equal(swatchOf(doc.getElementById('p1')).read(), '#00ff00');
  stroke.apply('#0000ff');
  assert.equal(app.selectedLineIdx, 0);
  assert.equal(app.lines[0].color, '#0000ff');
  assert.deepEqual(calls.filter(([k]) => k === 'save'), [['save']], 'one undo step');
  const point = swatchOf(doc.getElementById('p1'));
  point.apply('#123456');
  assert.equal(app.lines[1].pointColor, '#123456');
  assert.equal(app.selectedLineIdx, 0, 'a point swatch leaves the selection where it was');
  app.readOnly = true;
  assert.equal(stroke.enabled(), false, 'the read-only compare view takes no drop');
});

test('the live swatches are the shown, enabled ones, each once', (t) => {
  const doc = page(`<input type="color" id="a" value="#000000"><input type="color" id="b" disabled>
    <input type="color" id="c" style="display:none"><label class="vs-color" id="w"><input type="color" id="d"></label>`);
  t.after(() => doc.restore());
  doc.getElementById('c').style.display = 'none';
  const ids = liveSwatches(doc).map((s) => s.el.id).filter((id) => id !== 'blank-color-btn');
  assert.deepEqual(ids.sort(), ['a', 'w']);
});

test('every swatch the Lines tab paints is one a colour drag knows, for the same line colour', async () => {
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../../../js/ui/panel/lines/events.js', import.meta.url), 'utf8');
  const table = /const SWATCHES = Object\.freeze\((\[[^;]*\])\);/.exec(src);
  assert.ok(table, 'the Lines tab names its swatch classes in one table');
  const pairs = [...table[1].matchAll(/\['([\w-]+)', '(\w+)'\]/g)].map(([, cls, prop]) => [cls, prop]);
  assert.ok(pairs.length > 0);
  assert.deepEqual(Object.fromEntries(pairs), ROW_SWATCHES);
});
