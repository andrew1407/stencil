// While CLI mode is on, every window, chat, fullscreen, tooltip and look-or-motion member of
// window.stencil refuses with one message, and every edit still goes through.
import { test, after } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp, lastCall } from '../helpers/stencilApiRig.js';
import { setCliMode, GATED_SETTINGS } from '../../js/console/shell/gate.js';

after(() => setCliMode(false));

test('the UI members refuse from the one gate; the edits do not', () => {
  const app = makeApp({ image: { width: 4, height: 2 }, chat: { open() {}, close() {}, dock() {}, isOpen: () => false, setVoiceInput() {} }, voice: {} });
  Object.assign(app.settings, { setMotion: () => ({}), setNotifyChannel() {}, setVisualColor() {} });
  const stencil = createStencil(app);
  setCliMode(true);
  const refused = /stencil: CLI mode is on — .* waits until \/clioff/;
  assert.throws(() => stencil.openWindow('projects'), refused);
  assert.throws(() => stencil.openVisualsWindow(), refused);
  assert.throws(() => stencil.openIn(), refused);
  assert.throws(() => stencil.chat.open(), refused);
  assert.throws(() => stencil.chat.dock('left'), refused);
  assert.throws(() => { stencil.chat.voiceInput = true; }, refused);
  assert.throws(() => { stencil.fullscreen = true; }, refused);
  assert.throws(() => { stencil.tooltip.enabled = false; }, refused);
  assert.throws(() => stencil.startDrawing(), refused);
  assert.throws(() => { stencil.drawing = true; }, refused);
  assert.throws(() => { stencil.voiceChat = true; }, refused);
  assert.throws(() => stencil.EasterEggs.neonOn(), refused);
  assert.throws(() => { stencil.EasterEggs.waterShowMode = true; }, refused);
  for (const key of GATED_SETTINGS) {
    assert.throws(() => { stencil[key] = stencil[key]; }, refused, `${key} on the facade`);
    assert.throws(() => { stencil.settings[key] = stencil.settings[key]; }, refused, `${key} under .settings`);
  }

  stencil.lineColor = '#123456';
  assert.deepEqual(lastCall(app, 'setColor'), ['setColor', '#123456']);
  stencil.filter = 'bw';
  assert.deepEqual(lastCall(app, 'setImageFilter'), ['setImageFilter', 'bw']);
  stencil.rotateRight();
  assert.ok(lastCall(app, 'rotateImage'));
  stencil.darkTheme = true;
  assert.deepEqual(lastCall(app, 'setTheme'), ['setTheme', 'dark']);
  stencil.mainTheme = 'sky';
  assert.deepEqual(lastCall(app, 'setAccent'), ['setAccent', 'sky']);
  stencil.undo();
  assert.ok(lastCall(app, 'undo'));
  assert.doesNotThrow(() => stencil.closeWindow());
  assert.doesNotThrow(() => stencil.chat.close());
  assert.equal(stencil.EasterEggs.cliMode, true);
  setCliMode(false);
  assert.doesNotThrow(() => { stencil.motionSpeed = 0.5; });
});
