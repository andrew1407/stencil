// CLI mode (js/ui/cli/mode.js + stage.js): entering closes the stage, every shell, the chat and
// fullscreen, sets the gate and puts the terminal up; a command in it edits for real; /clioff and
// the hold leave it with the edits kept; the facade's cliMode switch does the same.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { installMemoryStorage } from '../../helpers/memoryStorage.js';

installMemoryStorage();
let doc, body;
beforeEach(() => {
  body = createStubElement('body', { querySelectorAll: () => [] });
  const balloon = createStubElement('stencil-notifications', { notify() {} });
  doc = installDom({ body, getElementById: (id) => (id === 'notify-balloon' ? balloon : null), querySelectorAll: () => [] },
    { innerWidth: 1000, innerHeight: 600, requestAnimationFrame: () => 1, cancelAnimationFrame: () => {}, performance: { now: () => 0 } });
  doc.querySelector = () => null;
  globalThis.window = globalThis.window || {};
  globalThis.window.addEventListener ??= () => {};
  globalThis.window.removeEventListener ??= () => {};
  globalThis.window.dispatchEvent ??= () => {};
});

const { createStencil, makeApp } = await import('../../helpers/stencilApiRig.js');
const { enterCliMode, exitCliMode, cliModeActive } = await import('../../../js/ui/cli/mode.js');
const { cliModeOn } = await import('../../../js/console/shell/gate.js');
const { wireModalShell } = await import('../../../js/ui/base.js');
const { themeSwap } = await import('../../../js/ui/motion/surface/themeSwapPlay.js');

const rig = () => {
  const closed = [];
  const app = makeApp({ image: { width: 4, height: 2 }, chat: { close: () => closed.push('chat'), open() {}, isOpen: () => false } });
  app.accents.themeMode = 'system';
  app.accents.setThemeMode = (m) => { app.accents.themeMode = m; };
  app.changes = { on: () => () => {} };
  const stencil = createStencil(app);
  window.stencil = stencil;
  return { app, stencil, closed };
};

test('entering clears the way, gates the facade and puts the stage up; leaving takes it down', () => {
  const { app, stencil, closed } = rig();
  const shell = wireModalShell(createStubElement('div', { id: 'w', getBoundingClientRect: () => ({ left: 0, top: 0, width: 10, height: 10, bottom: 10 }) }), null, null);
  shell.open();
  assert.equal(enterCliMode(app), true);
  assert.equal(cliModeActive(), true);
  assert.equal(shell.isOpen(), false, 'the window gave way');
  assert.deepEqual(closed, ['chat']);
  assert.ok(body.children.some((c) => c.className === 'cli-stage'));
  assert.ok(body.classList.contains('cli-mode-open'));
  assert.throws(() => stencil.openProjectsWindow(), /CLI mode is on/);
  assert.equal(enterCliMode(app), false, 'already on');
  assert.equal(exitCliMode(app), true);
  assert.equal(cliModeOn(), false);
  assert.ok(!body.children.some((c) => c.className === 'cli-stage'));
  assert.equal(exitCliMode(app), false);
});

test('a command typed in the stage edits the project for real, and /clioff leaves it', async () => {
  const { app, stencil } = rig();
  stencil.EasterEggs.cliMode = true;
  assert.equal(stencil.EasterEggs.cliMode, true);
  const stage = body.children.find((c) => c.className === 'cli-stage');
  const input = stage.children.find((c) => c.className === 'cli-prompt-row').children[1];
  input.value = '/rotate 1';
  input.dispatch('keydown', { key: 'Enter', preventDefault() {} });
  await new Promise((r) => setTimeout(r, 0));
  assert.deepEqual(app.calls.at(-1), ['rotateImage', 1]);
  input.value = '/clioff';
  input.dispatch('keydown', { key: 'Enter', preventDefault() {} });
  await new Promise((r) => setTimeout(r, 0));
  assert.equal(cliModeOn(), false);
  assert.deepEqual(app.calls.filter((c) => c[0] === 'rotateImage').length, 1, 'the edit stays');
});

test('while CLI mode is on, a theme or accent change lands at once: no flood, no dust', () => {
  const { app } = rig();
  let transitions = 0;
  doc.startViewTransition = (fn) => { transitions++; fn(); return { finished: Promise.resolve(), ready: Promise.resolve() }; };
  doc.documentElement = createStubElement('html');
  enterCliMode(app);
  let applied = 0;
  themeSwap(() => applied++);
  assert.equal(applied, 1);
  assert.equal(transitions, 0, 'the stage paints the recolour itself');
  exitCliMode(app);
});
