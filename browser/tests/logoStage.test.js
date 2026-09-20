// The logo stage itself (js/ui/logoStage.js): when it may open, the lock it puts on the
// keyboard while it is up, and what a click on the mark does versus a click beside it.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from './helpers/dom.js';

let doc, body, frames;
const html = createStubElement('html');

const setup = ({ fullscreen = false } = {}) => {
  frames = [];
  body = createStubElement('body', { classes: new Set(fullscreen ? ['fullscreen-mode'] : []) });
  body.classList = {
    add: (c) => body.classes.add(c), remove: (c) => body.classes.delete(c),
    contains: (c) => body.classes.has(c), toggle: (c, on) => (on ? body.classes.add(c) : body.classes.delete(c)),
  };
  doc = installDom({ body, documentElement: html, querySelector: () => null });
  globalThis.window = globalThis.window || {};
  globalThis.innerWidth = 1000; globalThis.innerHeight = 600;
  globalThis.devicePixelRatio = 1;
  globalThis.requestAnimationFrame = (fn) => { frames.push(fn); return frames.length; };
  globalThis.cancelAnimationFrame = () => {};
  globalThis.performance = { now: () => 0 };
};

beforeEach(setup);

const { openLogoStage, closeLogoStage, logoStageOpen, logoStageAllowed, currentLogoStage } =
  await import('../js/ui/logoStage.js');
const { modalShells } = await import('../js/ui/modalRegistry.js');

const app = { accent: 'violet', customAccent: null };
const open = (name = 'neonOn') => openLogoStage(name, { app, origin: { x: 20, y: 20 }, doc });

test('it opens over the bare window and hangs one host on the body', () => {
  assert.equal(open(), true);
  assert.equal(logoStageOpen(), true);
  assert.equal(body.children.length, 1);
  assert.equal(body.children[0].className, 'logo-stage');
  assert.equal(body.children[0].getAttribute('aria-hidden'), 'true');
  assert.equal(currentLogoStage().effect, 'neon');
  closeLogoStage();
});

test('a second open is refused while one is up, and the pink show never opens a stage', () => {
  assert.equal(open(), true);
  assert.equal(open('makeSomeSunshine'), false, 'one stage at a time');
  closeLogoStage();
  assert.equal(openLogoStage('pinkVibe', { app, doc }), false, 'pink is an edit, not a stage');
  assert.equal(openLogoStage('nosuch', { app, doc }), false);
});

test('fullscreen and an open window both refuse it', () => {
  setup({ fullscreen: true });
  assert.equal(logoStageAllowed(doc), false, 'not in fullscreen');
  setup();
  const shell = { isOpen: () => true, close() {}, open() {} };
  modalShells.add(shell);
  assert.equal(logoStageAllowed(doc), false, 'not with a window open');
  modalShells.delete(shell);
  assert.equal(logoStageAllowed(doc), true);
});

test('while it is up the keyboard belongs to it — every key but Escape is swallowed', () => {
  open();
  const seen = [];
  const swallowed = (key) => {
    let stopped = false;
    const ev = { type: 'keydown', key, preventDefault() {}, stopImmediatePropagation() { stopped = true; } };
    for (const fn of doc.listeners.keydown) fn(ev);
    seen.push([key, stopped]);
    return stopped;
  };
  assert.equal(swallowed('z'), true, 'a plain key never reaches the editor');
  assert.equal(swallowed('Delete'), true);
  assert.equal(swallowed('Escape'), false, 'Escape is the way out, not a swallowed key');
  assert.equal(logoStageOpen(), false, '…and it closed the stage');
  assert.equal(seen.length, 3);
});

test('a press beside the mark closes it; a press ON the mark is a hold, and holds it open', () => {
  open('randomWalk');
  const host = body.children[0];
  const { x, y } = currentLogoStage().position;
  host.dispatch('pointerdown', { clientX: 0, clientY: 0, preventDefault() {} });
  assert.equal(logoStageOpen(), false, 'beside the mark: closed');

  for (const name of ['firework', 'neonOn', 'makeItSmall']) {
    open(name);
    const stage = currentLogoStage();
    stage.host.dispatch('pointerdown', { clientX: x, clientY: y, preventDefault() {} });
    assert.equal(logoStageOpen(), true, `${name}: a press on the mark never closes it`);
    stage.host.dispatch('pointerup', {});
    assert.equal(logoStageOpen(), true, `${name}: and letting go does not either`);
    closeLogoStage();
  }
});

test('the mark wears the hand, and only the mark', () => {
  open();
  const stage = currentLogoStage();
  const { x, y } = stage.position;
  const cursorAt = (cx, cy) => {
    stage.host.dispatch('pointermove', { clientX: cx, clientY: cy });
    return stage.host.style.cursor;
  };
  assert.equal(cursorAt(x, y), 'pointer', 'over the mark');
  assert.equal(cursorAt(0, 0), 'default', 'beside it');
  assert.equal(cursorAt(x, y), 'pointer', 'and back');
  closeLogoStage();
});

test('closing unhooks the document, so the editor gets its keyboard back', () => {
  open();
  const before = doc.listeners.keydown.length;
  closeLogoStage();
  assert.equal(doc.listeners.keydown.length, before - 1);
  assert.equal(closeLogoStage(), false, 'closing twice is a no-op');
});
