// The stage's status line (js/ui/cli/status.js): the name in the project colour, one click shows
// the editing picture and another hides it, a double-click shows the original and back.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubDocument, createStubElement } from '../../helpers/dom.js';
import { paintStatus, wireStatusGestures } from '../../../js/ui/cli/status.js';
import { createCliPeek } from '../../../js/ui/cli/peek.js';

const app = () => ({
  canvas: { width: 1280, height: 720 }, image: { width: 10, height: 5 }, imageBaseName: 'pic', imageExt: 'png', pageSize: 'A4', activeProjectId: 'p',
  storage: { incognito: false, store: { getMeta: () => ({ name: 'Mine', color: '#00ff00' }) } },
  history: { history: [], historyStep: -1 },
  export: { renderExportCanvas: (v) => ({ toDataURL: () => `data:${v}` }) },
});

test('the name wears the project colour, and the gestures drive the peek', () => {
  const doc = createStubDocument();
  const el = createStubElement('div', { ownerDocument: doc });
  const s = paintStatus(el, app());
  assert.equal(s.name, 'Mine.png');
  const name = el.children[1];
  assert.equal(name.className, 'cli-status-name');
  assert.equal(name.style['--name-color'], '#00ff00', 'the colour as a variable, so a skin can paint over it');
  const slot = createStubElement('div', { ownerDocument: doc });
  const peekApp = app();
  const peek = createCliPeek(peekApp, slot);
  const timers = [];
  wireStatusGestures(el, peek, { schedule: (fn) => { timers.push(fn); return timers.length; }, cancel: () => timers.pop() });
  const click = (type) => el.dispatch(type, { target: createStubElement('span', { closest: () => name }) });
  click('click'); timers.shift()?.();
  assert.equal(peek.shown(), 'editing');
  assert.equal(slot.children[0].src, 'data:current');
  click('click'); timers.shift()?.();
  assert.equal(peek.shown(), null);
  click('dblclick');
  assert.equal(peek.shown(), 'original');
  assert.equal(slot.children[0].src, 'data:original');
  click('dblclick');
  assert.equal(peek.shown(), 'editing');
  peek.release();
  assert.equal(peek.shown(), 'editing', 'a release ends only a held peek');
  peek.hide();
  peek.hold('editing', { magnified: true });
  assert.equal(slot.children[0].classList.contains('is-magnified'), true);
  peek.release();
  assert.equal(peek.shown(), null);
  // Shift joining the held Alt magnifies the peek, and letting it go unmagnifies.
  peek.hold('editing', { magnified: false });
  assert.equal(slot.children[0].classList.contains('is-magnified'), false);
  peek.magnify(true);
  assert.equal(slot.children[0].classList.contains('is-magnified'), true);
  peek.magnify(false);
  assert.equal(slot.children[0].classList.contains('is-magnified'), false);
  peek.hold('editing', { magnified: true });
  assert.equal(slot.children[0].classList.contains('is-magnified'), true, 'a re-hold while held redraws with the new size');
  peek.release();
  peek.toggle('editing');
  peekApp.export.renderExportCanvas = (which) => ({ toDataURL: () => `data:${which}-after-edit` });
  peek.refresh();
  assert.equal(slot.children[0].src, 'data:current-after-edit', 'an edit landing redraws the picture that is up');
  peek.hide();
  peek.refresh();
  assert.equal(peek.shown(), null, 'nothing up, nothing drawn');
});
