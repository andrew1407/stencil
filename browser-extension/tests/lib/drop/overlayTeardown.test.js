// The three overlays the worker injects into a page (the editor modal, the drop zones, the drop
// chooser) are each replaced or removed by a later injection that shares none of their closures.
// Whatever removes one must also unhook it: no page listener, timer or pending answer outlives it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountStencilModal } from '../../../src/lib/drop/overlay.js';
import { mountDropZones, unmountDropZones } from '../../../src/lib/drop/zones.js';
import { mountDropChoice } from '../../../src/lib/drop/choice.js';
import { installDom, stubDoc, stubEl, stubWin } from '../../helpers/domStub.js';

// One page every injection lands in: getElementById finds a live host by its id.
const page = () => {
  const created = [];
  const sent = [];
  const storageListeners = new Set();
  const el = (tag) => {
    const found = {};
    const node = stubEl(tag, {
      contentWindow: { postMessage() {} },
      querySelector: (sel) => (found[sel] ||= el(sel)),
    });
    node.attachShadow = () => node;
    created.push(node);
    return node;
  };
  const win = stubWin();
  const doc = stubDoc({
    createElement: el,
    getElementById: (id) => created.find((n) => n.id === id && !n.removed) || null,
  });
  const restore = installDom({
    document: doc,
    window: win,
    chrome: {
      storage: { onChanged: { addListener: (fn) => storageListeners.add(fn), removeListener: (fn) => storageListeners.delete(fn) } },
      runtime: { sendMessage: (m) => { sent.push(m); } },
    },
  });
  const count = (type) => (win.listeners[type] || []).length;
  return { doc, win, sent, storageListeners, count, restore };
};

test('a modal replaced by a second launch leaves no listener and no fallback timer behind', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const p = page();
  try {
    mountStencilModal('https://editor.example/#stencil=a', 'Stencil', 8000, {});
    t.mock.timers.tick(4000);
    mountStencilModal('https://editor.example/#stencil=b', 'Stencil', 8000, {});
    assert.equal(p.count('message'), 1, 'the replaced modal still listens for frame messages');
    assert.equal(p.doc.on('keydown').length, 1, 'the replaced modal still listens for Escape');
    assert.equal(p.storageListeners.size, 1, 'the replaced modal still follows the theme');
    t.mock.timers.tick(5000);
    assert.deepEqual(p.sent, [], 'no tab opens while the live modal is within its own deadline');
  } finally { p.restore(); }
});

test('disarmed drop zones unhook the window and their timer, so the next arm owns the page', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const p = page();
  try {
    mountDropZones('#7c3aed', false, 'light');
    unmountDropZones();
    for (const type of ['dragenter', 'dragover', 'drop', 'dragleave', 'keydown'])
      assert.equal(p.count(type), 0, `a disarmed overlay still listens for ${type}`);
    t.mock.timers.tick(6000);
    mountDropZones('#7c3aed', false, 'light');
    t.mock.timers.tick(6500);
    assert.equal(globalThis.window.__stencilDropZones, true, 'a stale timer released the live overlay\'s guard');
    assert.equal(p.count('drop'), 1);
  } finally { p.restore(); }
});

test('a chooser replaced by a second one answers cancel and unhooks its key listener', async () => {
  const p = page();
  try {
    let firstAnswer = 'none';
    mountDropChoice('#7c3aed', 'light').then((v) => { firstAnswer = v; });
    const second = mountDropChoice('#7c3aed', 'light');
    await Promise.resolve();
    assert.equal(firstAnswer, 'cancel', 'the replaced chooser never answered its caller');
    assert.equal(p.count('keydown'), 1, 'the replaced chooser still listens for Escape');
    p.win.fire('keydown', { key: 'Escape', stopPropagation() {} });
    assert.equal(await second, 'cancel');
    assert.equal(p.count('keydown'), 0);
  } finally { p.restore(); }
});
