// The .stc runner maps the core's op stream onto the window.stencil facade. A fake facade
// records the calls, so this pins WHICH facade method each op reaches — the guard against
// the runner and the toolbar drifting into two code paths.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installDom } from './helpers/dom.js';

// notify() paints a toast, so the runner needs a document even headlessly.
installDom();

const { ScriptError, runScript } = await import('../js/console/scriptRunner.js');

const fakeStencil = (over = {}) => {
  const calls = [];
  const self = {
    calls,
    imageSize: { width: 200, height: 100 },
    // The real facade's Project handle: `current`, null on a blank editor.
    current: { incognito: false, name: 'shot', set name(v) { calls.push(['rename', v]); } },
    getProjectByName() { return null; },
    crop(spec) { calls.push(['crop', spec]); return self; },
    apply(opts) { calls.push(['apply', opts]); return self; },
    setLines(lines, opts) { calls.push(['setLines', lines, opts]); return self; },
    applyLayout(data, opts) { calls.push(['applyLayout', data, opts]); return self; },
    undo() { calls.push(['undo']); return self; },
    redo() { calls.push(['redo']); return self; },
    async load(url, opts) { calls.push(['load', url, opts]); return self; },
    async save() { calls.push(['save']); return self; },
    ...over,
  };
  return self;
};

const names = (s) => s.calls.map((c) => c[0]);

test('a crop op reaches stencil.crop with the edge tokens, not pixels', async () => {
  const s = fakeStencil();
  await runScript('@crop 10%', s);
  assert.deepEqual(names(s), ['crop']);
  assert.deepEqual(s.calls[0][1], { x1: '10%', x2: '-10%', y1: '10%', y2: '-10%' });
});

test('a filter op reaches stencil.apply, with a colour becoming a custom tint', async () => {
  const bw = fakeStencil();
  await runScript('@filter bw', bw);
  assert.deepEqual(bw.calls[0], ['apply', { filter: 'bw' }]);

  const tint = fakeStencil();
  await runScript('@filter #ff0000', tint);
  assert.deepEqual(tint.calls[0], ['apply', { filter: 'custom', filterColor: '#ff0000' }]);
});

test('a shape op resolves to pixels and combines into the lines', async () => {
  const s = fakeStencil();
  await runScript('@use line red dashed, 3px\n@rect (10,10) (-10%, -20%)', s);
  const [name, lines, opts] = s.calls[0];
  assert.equal(name, 'setLines');
  assert.deepEqual(opts, { mode: 'combine' });
  assert.equal(lines[0].color, 'red');
  assert.equal(lines[0].style, 'dashed');
  assert.equal(lines[0].thickness, 3);
  assert.equal(lines[0].locked, true);
  // Two corners become four, and '-10%' of a 200px image is 180.
  assert.equal(lines[0].points.length, 4);
  assert.deepEqual(lines[0].points[0], { x: 10, y: 10 });
  assert.deepEqual(lines[0].points[2], { x: 180, y: 80 });
});

test('undo and redo step the editor history the requested number of times', async () => {
  const s = fakeStencil();
  await runScript('@filter bw\n@rect (1,1) (2,2)\n@undo 1\n@save', s);
  // The core rewinds and replays, so the runner sees plain undo/redo steps only.
  assert.ok(names(s).includes('undo'));
  assert.equal(names(s).at(-1), 'save');
});

test('a named save renames the project first', async () => {
  const s = fakeStencil();
  await runScript('@save my-shot', s);
  assert.deepEqual(names(s), ['rename', 'save']);
});

test('a named save on a taken name saves beside it, and an incognito one just saves', async () => {
  const taken = fakeStencil({ getProjectByName: (n) => (n === 'my-shot' ? {} : null) });
  await runScript('@save my-shot', taken);
  assert.deepEqual(taken.calls[0], ['rename', 'my-shot 2']);

  const hidden = fakeStencil({ current: { incognito: true, name: 'Incognito (unsaved)' } });
  await runScript('@save my-shot', hidden);
  assert.deepEqual(names(hidden), ['save'], 'an incognito editor has no name to take');

  const blank = fakeStencil({ current: null });
  await runScript('@save my-shot', blank);
  assert.deepEqual(names(blank), ['save'], 'nothing to rename before a project exists');
});

test('a layout op fetches through the injected loader and applies it', async () => {
  const s = fakeStencil();
  const seen = [];
  await runScript('@layout https://example.com/l.json replace', s, {
    fetchLayout: async (src) => { seen.push(src); return { lines: [] }; },
  });
  assert.deepEqual(seen, ['https://example.com/l.json']);
  assert.deepEqual(s.calls[0], ['applyLayout', { lines: [] }, { mode: 'replace' }]);
});

test('a url source loads; a local path is refused with a usable message', async () => {
  const ok = fakeStencil();
  await runScript('@source https://example.com/a.png:\n  @filter bw', ok);
  assert.deepEqual(ok.calls[0], ['load', 'https://example.com/a.png', {}]);

  const bad = fakeStencil();
  await assert.rejects(
    () => runScript('@source ./local.png:\n  @filter bw', bad),
    (err) => err instanceof ScriptError && /needs a URL in the browser/.test(err.message),
  );
  assert.deepEqual(names(bad), [], 'nothing ran');
});

test('a script with an error runs nothing at all', async () => {
  const s = fakeStencil();
  await assert.rejects(
    () => runScript('@filter bw\n@crp 10%', s),
    (err) => err instanceof ScriptError && err.line === 2,
  );
  assert.deepEqual(names(s), [], 'not even the valid op before it');
});

test('a failure part-way reports the line and leaves earlier edits applied', async () => {
  const s = fakeStencil({
    apply() { throw new Error('canvas is gone'); },
  });
  await assert.rejects(() => runScript('@crop 10%\n@filter bw', s));
  assert.deepEqual(names(s), ['crop'], 'the crop before it stands');
});

test('every op kind the core emits has a runner branch', async () => {
  const s = fakeStencil();
  await runScript(
    '@source https://example.com/a.png:\n  @crop 10%\n  @filter bw\n  @line (1,1) (2,2)\n'
    + '  @rect (3,3) (4,4)\n  @save out',
    s,
  );
  const seen = new Set(names(s));
  for (const expected of ['load', 'crop', 'apply', 'setLines', 'save']) {
    assert.ok(seen.has(expected), `no runner branch reached ${expected}`);
  }

  // An @undo that survives to a @save becomes a real rewind; @undo + @redo cancel in the
  // core and correctly reach the runner as nothing at all.
  const undone = fakeStencil();
  await runScript('@filter bw\n@rect (1,1) (2,2)\n@undo\n@save', undone);
  assert.ok(names(undone).includes('undo'));

  const cancelled = fakeStencil();
  await runScript('@filter bw\n@rect (1,1) (2,2)\n@undo\n@redo\n@save', cancelled);
  assert.ok(!names(cancelled).includes('undo'), 'an undone-then-redone edit needs no rewind');
});
