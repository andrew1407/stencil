// The open context menu follows the editor through updateButtons instead of polling: every
// sweep notifies its followers, the unsubscribe stops it, and an unchanged tip is not rewritten.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { updateButtons, onButtonsUpdated } from '../../../js/ui/control/state.js';
import { makeControlApp } from '../../helpers/controlStateRig.js';

const makeApp = () => makeControlApp();

test('every sweep notifies the followers, until they unsubscribe', (t) => {
  const doc = installDom({ autoCreateById: true });
  t.after(() => doc.restore());
  let runs = 0;
  const off = onButtonsUpdated(() => { runs++; });
  updateButtons(makeApp());
  updateButtons(makeApp());
  assert.equal(runs, 2);
  off();
  updateButtons(makeApp());
  assert.equal(runs, 2, 'a closed menu stops following');
});

test('a recomposed tooltip that did not change is not written back', (t) => {
  const btn = createStubElement('button');
  btn.dataset.title = 'Undo';
  btn.dataset.hkTitle = 'undo';
  let writes = 0;
  let tip = '';
  Object.defineProperty(btn.dataset, 'tip', { get: () => tip, set: (v) => { writes++; tip = v; } });
  const doc = installDom({ autoCreateById: true, querySelectorAll: () => [btn] });
  t.after(() => doc.restore());
  updateButtons(makeApp());
  updateButtons(makeApp());
  assert.equal(writes, 1, 'the second sweep leaves the same tip alone');
});
