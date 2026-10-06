// The drawing toggle: Alt+A both starts and stops (core/draw/mode.js toggleDrawing), and ~ pressed
// twice does the same (ui/bindings/keys/drawDoublePress.js). Desktop twin: MainWindowEvents.cpp.
import test from 'node:test';
import assert from 'node:assert';
import HOTKEY_DEFS from '../../../../../common/config/hotkeysConfig.json' with { type: 'json' };
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

import { createDoublePress, wireDrawDoublePress } from '../../../../js/ui/bindings/keys/drawDoublePress.js';

const { doubleTapMs } = constants.POPOVER;

test('one Alt+A binding toggles drawing; Alt+S no longer stops it', () => {
  const ids = HOTKEY_DEFS.map((d) => d.id);
  assert.ok(!ids.includes('stopDraw'));
  assert.equal(HOTKEY_DEFS.find((d) => d.id === 'startDraw').default, 'Alt+A');
});

test('a double-press is two presses inside the window; a third starts over', () => {
  const press = createDoublePress(300);
  assert.equal(press(0), false);
  assert.equal(press(200), true);
  assert.equal(press(250), false, 'the press after a double starts a new count');
  assert.equal(press(1000), false, 'too slow: a new first press');
  assert.equal(press(1290), true);
});

const rig = (over = {}) => {
  const handlers = [];
  const doc = { addEventListener: (type, fn) => { if (type === 'keydown') handlers.push(fn); } };
  const app = { isDrawing: false, image: {}, compareReadOnly: () => false, ...over };
  wireDrawDoublePress(app, doc);
  const key = (timeStamp, more = {}) => handlers.forEach((fn) => fn({
    code: 'Backquote', timeStamp, repeat: false, ctrlKey: false, metaKey: false, altKey: false,
    target: { tagName: 'CANVAS' }, preventDefault() {}, ...more,
  }));
  return { app, key };
};

test('~ twice toggles drawing both ways; one press does nothing', () => {
  const { app, key } = rig({
    selectedLineIdx: -1, lines: [], currentLine: null, continueLineIdx: -1, undonePoints: [],
    color: '#f00', thickness: 2, pointSize: 4, style: 'solid',
    coordTable: { update() {} }, renderer: { redraw() {} }, hideSelectionPanels() {}, saveHistory() {},
  });
  key(0);
  assert.equal(app.isDrawing, false, 'one press does nothing');
  key(doubleTapMs - 10);
  assert.equal(app.isDrawing, true, 'the second press inside the window starts drawing');
  key(5000);
  key(5000 + doubleTapMs - 10);
  assert.equal(app.isDrawing, false, 'and the next double-press stops it');
});

test('~ is ignored with a modifier, in a text field, on auto-repeat and in a read-only compare view', () => {
  for (const more of [{ altKey: true }, { ctrlKey: true }, { metaKey: true }, { repeat: true },
    { target: { tagName: 'INPUT', type: 'text' } }]) {
    const { app, key } = rig({ isDrawing: true });
    key(0, more);
    key(50, more);
    assert.equal(app.isDrawing, true, JSON.stringify(more));
  }
  const { app, key } = rig({ isDrawing: true, compareReadOnly: () => true });
  key(0);
  key(50);
  assert.equal(app.isDrawing, true, 'read-only compare view');
});
