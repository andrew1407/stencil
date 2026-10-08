// Dragging a colour swatch onto another (js/ui/drag/colorDrag.js): the chip and the glow while it is
// live, a drop handing the colour over through the target's own change path, and the drops that do
// nothing — the source itself, a non-swatch, a swatch that already shows the colour.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installMenuDom } from '../../helpers/menuDom.js';
import { colorDragHooks, wireColorDrag } from '../../../js/ui/drag/colorDrag.js';
import { TARGET_CLASS, TARGET_OVER_CLASS } from '../../../js/ui/drag/iconDrag.js';

const rig = (t) => {
  const doc = installMenuDom();
  doc.body.innerHTML = `<input type="color" id="line-color" value="#ff0000">
    <div class="control-group"><input type="color" id="sel-color" value="#00ff00"><input class="alpha-input" id="sel-alpha" value="255"></div>
    <input type="color" id="point-color" value="#ff0000"><span id="plain"></span>`;
  t.after(() => doc.restore());
  const chips = [];
  const chipFor = (color) => {
    const chip = { color, at: null, gone: false, move(x, y) { chip.at = [x, y]; }, destroy() { chip.gone = true; } };
    chips.push(chip);
    return chip;
  };
  const $ = (id) => doc.getElementById(id);
  const changes = [];
  for (const id of ['line-color', 'sel-color', 'point-color'])
    $(id).addEventListener('change', () => changes.push(`${id}:${$(id).value}`));
  return { doc, $, chips, chipFor, changes };
};
const glow = (el) => (el.classList.contains(TARGET_OVER_CLASS) ? 'over' : el.classList.contains(TARGET_CLASS) ? 'on' : '');

test('a drag lifts a chip of the colour and lights every other swatch, the hovered one brighter', (t) => {
  const { $, chips, chipFor } = rig(t);
  const h = colorDragHooks($('line-color'), { chipFor });
  assert.equal(h.start({ x: 10, y: 20 }), true);
  assert.equal(chips[0].color, '#ff0000');
  assert.deepEqual(chips[0].at, [10, 20]);
  assert.deepEqual([glow($('line-color')), glow($('sel-color')), glow($('point-color'))], ['', 'on', 'on']);
  h.move({ x: 40, y: 50, target: $('sel-color') });
  assert.equal(glow($('sel-color')), 'over');
  h.move({ x: 60, y: 50, target: $('plain') });
  assert.equal(glow($('sel-color')), 'on', 'leaving a target dims it back');
  h.cancel();
  assert.equal(chips[0].gone, true);
  assert.deepEqual([glow($('sel-color')), glow($('point-color'))], ['', '']);
});

test('released on another swatch, it takes the colour through its own change, once', (t) => {
  const { $, chipFor, changes } = rig(t);
  const h = colorDragHooks($('line-color'), { chipFor });
  h.start({ x: 0, y: 0 });
  h.drop({ x: 5, y: 5, target: $('sel-color') });
  assert.deepEqual(changes, ['sel-color:#ff0000']);
  assert.equal($('sel-alpha').value, '255', 'an RGB source keeps the target alpha');
  assert.equal(glow($('sel-color')), '');
});

test('a drop on the source, on no swatch, or on one already that colour changes nothing', (t) => {
  const { $, chipFor, changes } = rig(t);
  for (const target of [$('line-color'), $('plain'), $('point-color'), null]) {
    const h = colorDragHooks($('line-color'), { chipFor });
    h.start({ x: 0, y: 0 });
    h.drop({ x: 5, y: 5, target });
  }
  assert.deepEqual(changes, []);
});

test('a swatch in an open window lights only the swatches of that window', (t) => {
  const { doc, $, chipFor } = rig(t);
  doc.body.insertAdjacentHTML('beforeend', `<div class="app-modal-overlay"><label class="vs-color" id="well">
    <input type="color" id="vs-fill" value="#010203"></label><input type="color" id="vs-glow" value="#040506"></div>`);
  const h = colorDragHooks($('vs-fill'), { chipFor });
  h.start({ x: 0, y: 0 });
  assert.deepEqual([glow($('vs-glow')), glow($('line-color')), glow($('sel-color'))], ['on', '', '']);
  h.cancel();
});

test('a disabled or colourless swatch refuses the drag', (t) => {
  const { $, chips, chipFor } = rig(t);
  $('line-color').disabled = true;
  assert.equal(colorDragHooks($('line-color'), { chipFor }).start({ x: 0, y: 0 }), false);
  assert.equal(colorDragHooks($('plain'), { chipFor }).start({ x: 0, y: 0 }), false);
  assert.deepEqual(chips, []);
});

test('a swatch is wired as a drag source on its first press, once; other presses wire nothing', (t) => {
  const { doc, $ } = rig(t);
  wireColorDrag({ lines: [] }, doc);
  const [capture] = doc.listeners('pointerdown');
  assert.equal(capture.capture, true);
  const press = (el) => capture.fn({ target: el });
  press($('plain'));
  assert.equal($('plain').listeners('pointerdown').length, 0);
  press($('sel-color'));
  press($('sel-color'));
  assert.equal($('sel-color').listeners('pointerdown').length, 1);
});
