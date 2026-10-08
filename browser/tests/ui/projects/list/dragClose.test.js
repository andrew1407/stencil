// The projects window's ✕ as a drop target (js/ui/projects/list/dragClose.js): only the project
// open in this tab arms it — glowing while held, brighter under the pointer — and a drop there
// closes it at once, unasked; any other row leaves the ✕ inert and its drop unclaimed.
import test from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../../helpers/dom.js';

installDom();
const { createDragClose } = await import('../../../../js/ui/projects/list/dragClose.js');
const { TARGET_CLASS, TARGET_OVER_CLASS } = await import('../../../../js/ui/drag/iconDrag.js');

const X = { left: 500, top: 10, right: 560, bottom: 34, width: 60, height: 24 };
const rig = (answer = true) => {
  const asked = [];
  const app = {
    activeProjectId: 'p1', closed: [],
    storage: { store: { getMeta: (id) => ({ id, name: 'Open one' }) } },
    confirm: async (message, opts) => { asked.push({ message, opts }); return answer; },
    closeProject(id) { app.closed.push(id); },
  };
  const closeBtn = createStubElement('button', { getBoundingClientRect: () => X });
  let settled = 0;
  const target = createDragClose({ app, closeBtn, settle: () => { settled++; } });
  return { app, asked, closeBtn, target, settled: () => settled };
};
const tick = () => new Promise((r) => setImmediate(r));

test('the open project arms the ✕: it glows, brightens under the pointer, and a drop closes it unasked', async () => {
  const { app, asked, closeBtn, target, settled } = rig();
  target.begin({ meta: { id: 'p1' }, isRemote: false });
  assert.ok(closeBtn.classes.has(TARGET_CLASS), 'held over nothing yet, the ✕ already reads as a target');
  assert.equal(target.track(100, 100), false);
  assert.ok(!closeBtn.classes.has(TARGET_OVER_CLASS));
  assert.equal(target.track(530, 20), true, 'over the ✕ the drop is its own');
  assert.ok(closeBtn.classes.has(TARGET_OVER_CLASS));
  assert.equal(target.drop(530, 20), true);
  assert.ok(!closeBtn.classes.has(TARGET_CLASS), 'the glow goes with the drop');
  await tick();
  assert.equal(asked.length, 0, 'no question');
  assert.deepEqual(app.closed, ['p1']);
  assert.equal(settled(), 1, 'the list re-settles once closed');
  target.end();
});

test('any other row leaves the ✕ inert: no glow, no claim, nothing asked', async () => {
  for (const held of [{ meta: { id: 'p2' }, isRemote: false }, { meta: { id: 'p1' }, isRemote: true }, null]) {
    const { app, asked, closeBtn, target } = rig();
    target.begin(held);
    assert.ok(!closeBtn.classes.has(TARGET_CLASS));
    assert.equal(target.track(530, 20), false);
    assert.equal(target.drop(530, 20), false);
    await tick();
    assert.equal(asked.length, 0);
    assert.deepEqual(app.closed, []);
  }
});

test('a drop beside the ✕ is not its own, and the drag ending takes the glow away', () => {
  const { closeBtn, target } = rig();
  target.begin({ meta: { id: 'p1' }, isRemote: false });
  assert.equal(target.drop(400, 20), false);
  target.end();
  assert.ok(!closeBtn.classes.has(TARGET_CLASS) && !closeBtn.classes.has(TARGET_OVER_CLASS));
  assert.equal(target.track(530, 20), false, 'disarmed until the next pickup');
});
