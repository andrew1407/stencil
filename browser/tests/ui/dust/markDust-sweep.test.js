// The window dust sweep (js/ui/motion.js sweepDust): a cloud on <body> outlives its window, so
// each window sweeps its own on the way out, and a closed window starts no row cloud — driven
// through the modal shell, the chat panel and the points panel on a stub page. Split from
// markDust-bar.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { installDustDom, rect, boxEl } from '../../helpers/dustRig.js';

const dust = installDustDom({
  docOpts: { autoCreateById: true },
  globals: { window: { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {}, dispatchEvent() {} } },
});
dust.doc.els.set('panel-resizer', null);
const { surfaceIn, disintegrate } = await import('../../../js/ui/motion.js');
const { wireModalShell } = await import('../../../js/ui/base.js');
const { StencilMainContent } = await import('../../../js/ui/panel/mainContent.js');
const { StencilChatPanel } = await import('../../../js/ui/chat/panel.js');
const { wireOpenState } = await import('../../../js/ui/chat/panel/openState.js');

// A cloud that came out of a row inside `scope` (an open window), as disintegrate builds it.
const rowCloud = (scope) => { const row = boxEl(rect(0, 0, 200, 30), { closest: () => scope }); disintegrate(row, {}); return row.__dustHost; };
const sweepable = () => { dust.doc.querySelectorAll = (sel) => (sel === '.disintegrate-host' ? dust.clouds() : []); };

// A cloud is parented to <body> to clear the scroller, so it outlives its window: each window
// sweeps its own on the way out, before its own close flight (user report).
test('sweepDust takes down the clouds a window started, and only those', async () => {
  const { sweepDust } = await import('../../../js/ui/motion.js');
  const made = [];
  const host = (scope) => {
    const stopped = { stopped: false };
    const el = {
      className: 'disintegrate-host',
      dataset: scope ? { dustScope: scope } : {},
      __stop: () => { stopped.stopped = true; },
      remove: () => { el.removed = true; },
      removed: false,
      stopped,
    };
    made.push(el);
    return el;
  };
  const mine = host('projects-modal-overlay');
  const alsoMine = host('projects-modal-overlay');
  const anothers = host('connect-modal-overlay');
  const loose = host(null);
  const realDoc = globalThis.document;
  globalThis.document = { querySelectorAll: () => made };
  try {
    assert.strictEqual(sweepDust('projects-modal-overlay'), 2, 'both of that window\'s clouds');
    assert.ok(mine.removed && mine.stopped.stopped, 'the layer goes, and its loop stops first');
    assert.ok(alsoMine.removed);
    assert.ok(!anothers.removed, 'another window\'s cloud is left alone');
    assert.ok(!loose.removed, '…and so is one that belongs to no window');
    assert.strictEqual(sweepDust(null), 0, 'no scope, nothing swept');
    assert.strictEqual(sweepDust({ id: 'connect-modal-overlay' }), 1, 'an element works too');
  } finally {
    globalThis.document = realDoc;
  }
});

// …and the shell is what calls it: every window closes the same way (shell.js close()).
test('every modal shell sweeps its own dust as it closes', () => {
  dust.reset();
  sweepable();
  const box = boxEl(rect(300, 200, 400, 300));
  const overlay = createStubElement('div', { id: 'projects-modal-overlay', matches: () => true,
    querySelector: (sel) => (sel === '.app-modal' ? box : null) });
  const shell = wireModalShell(overlay, null, null, { originEl: () => boxEl(rect(10, 10, 20, 20)) });
  shell.open();
  const mine = rowCloud(overlay);
  const anothers = rowCloud(createStubElement('div', { id: 'connect-modal-overlay', matches: () => true }));
  shell.close();
  assert.equal(mine.parentNode, null, 'the shared close sweeps, so no window has to remember to');
  assert.ok(anothers.parentNode, 'another window\'s cloud is left alone');
  assert.ok(box.__dustHost?.parentNode, 'and the window\'s own close flight starts after the sweep');
});

// A settle that lands mid-close (a removed project's list re-rendering) must not start a row cloud
// the sweep has already passed; a window's own flight is not a row and is exempt.
test('a row cloud starts only while its window is open', async () => {
  const { disintegrate, dustEnabled } = await import('../../../js/ui/motion.js');
  assert.ok(dustEnabled(), 'the node default flies particles');
  const scope = (open, dustScope) => ({ dataset: dustScope ? { dustScope } : {}, matches: () => open });
  const row = (s) => ({ closest: () => s, getBoundingClientRect: () => ({ left: 0, top: 0, width: 80, height: 20 }) });
  const realDoc = globalThis.document;
  let built = 0;
  globalThis.document = { body: {}, createElement: () => { built++; throw new Error('stub'); } };
  const start = (s) => disintegrate(row(s), { paintTile: () => {} });
  try {
    start(scope(true));
    assert.strictEqual(built, 1, 'an open window builds its cloud');
    assert.strictEqual(start(scope(false)), false);
    assert.strictEqual(start(scope(false, '.chat-open:not(.chat-closing)')), false);
    assert.strictEqual(built, 1, 'a closing modal or panel builds none');
  } finally {
    globalThis.document = realDoc;
  }
  // A surface flight is not gated: a menu leaving its closing window still plays.
  dust.reset();
  const menu = boxEl(rect(0, 0, 120, 80), { closest: () => scope(false) });
  assert.equal(surfaceIn(menu, { x: 0, y: 0 }), true, 'a surface flight is not gated');
});

// The non-modal windows that host row clouds declare their open state and sweep on the way out.
test('the chat panel and the coordinates panel scope and sweep their row clouds', () => {
  assert.match(StencilChatPanel.template(), /id="chat-panel" class="chat-panel" data-dust-scope="\.chat-open:not\(\.chat-closing\)"/);
  dust.reset();
  sweepable();
  const host = dust.doc.register('chat-panel', createStubElement('div', { querySelectorAll: () => [], matches: () => true,
    getBoundingClientRect: () => (host.classes.has('chat-open') ? rect(0, 0, 360, 800) : rect(0, 0, 0, 0)) }));
  const { setOpen } = wireOpenState({ host, input: createStubElement('textarea'), openBtn: null, resizer: null,
    header: null, backdrop: null, closeBtn: createStubElement('button'), panelIsOpen: () => host.classes.has('chat-open'),
    refreshStatus() {}, invalidatePillRects() {} });
  setOpen(true);
  const chatRow = rowCloud(host);
  setOpen(false);
  assert.equal(chatRow.parentNode, null, 'the chat sweeps its rows\' clouds as it closes…');
  assert.ok(host.__dustHost?.parentNode, '…before its own close flight');
  assert.match(StencilMainContent.inner(), /id="coord-panel" data-dust-scope=":not\(\.coord-collapsed\)"/);
  dust.reset();
  sweepable();
  const panel = dust.doc.register('coord-panel', createStubElement('div', { matches: () => true }));
  dust.doc.register('coord-body', boxEl(rect(900, 116, 300, 464)));
  dust.doc.getElementById('toggle-coord-panel').parentElement = boxEl(rect(900, 80, 300, 28));
  StencilMainContent.prototype.wire.call(createStubElement('stencil-main-content'), null);
  const toggle = dust.doc.getElementById('toggle-coord-panel');
  let row = rowCloud(panel);
  toggle.dispatch('click');
  assert.equal(row.parentNode, null, 'folding the panel away sweeps its rows\' clouds');
  assert.ok(panel.__dustHost?.parentNode, '…before its own fold flies');
  row = rowCloud(panel);
  toggle.dispatch('click');
  assert.ok(row.parentNode, 'opening it sweeps nothing');
});
