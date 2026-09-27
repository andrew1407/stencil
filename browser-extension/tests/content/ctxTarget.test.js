// content/ctxTarget.js — the right-click probe on <all_urls>: when it resolves (see
// ctxResolve.test.js for what) and what that costs a page. It runs on every page and frame, so
// a pointer in motion must cost nothing but a timer reset.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { MSG } from '../../src/lib/messages.js';
import { CTX_PROBE_FILES } from '../../src/background/registrars.js';
import { el, at, probe, PROBE_FILES, SOURCES } from '../helpers/ctxProbeEnv.js';

test('the resolver loads before the probe, the same way from the manifest and into open tabs', () => {
  assert.deepEqual(PROBE_FILES, ['src/content/ctxResolve.js', 'src/content/ctxTarget.js']);
  assert.deepEqual(CTX_PROBE_FILES, PROBE_FILES);
});

test('loading the probe wakes the lazy worker, so the menu exists before the first click', () => {
  const { sent } = probe();
  assert.equal(sent[0].type, MSG.WAKE);
  // The mirror inside the content script must be the canonical table's spelling.
  const src = SOURCES.at(-1);
  assert.ok(src.includes(`WAKE: '${MSG.WAKE}'`) && src.includes(`CTX: '${MSG.CTX}'`));
});

// The menu group is revealed by UPDATING the menu, which races Chrome's render, so a pointer
// that comes to rest resolves ahead of the click. Ten tiles of the same background cost one message.

test('resting on an element resolves early, and repeats of the same find are deduped', () => {
  const tile = () => el('div', { bg: 'url("/hero.png")' });
  const { fire, rest, ctxSent } = probe();
  for (let i = 0; i < 10; i++) { fire('pointerover', { target: tile(), clientX: i, clientY: 0 }); rest(); }
  assert.equal(ctxSent().length, 1, 'one message for ten identical tiles');
  assert.deepEqual(JSON.parse(JSON.stringify(ctxSent()[0].data)), { url: 'https://shop.example/hero.png' });
});

test('a pointer in motion resolves nothing: each crossing only re-arms one timer', () => {
  const { fire, timers, cost, ctxSent, listeners } = probe();
  for (let i = 0; i < 50; i++) fire('pointerover', { target: el('div', { bg: `url("/t${i}.png")` }), clientX: i, clientY: 0 });
  assert.equal(timers.size, 1, 'one armed dwell, however many crossings');
  assert.equal(cost.styles, 0, 'no computed style is read while moving');
  assert.equal(ctxSent().length, 0, 'the worker is not woken while moving');
  assert.equal((listeners.pointermove || []).length, 1);
});

test('no page-wide observer runs, and pointermove is heard once, for a pointer already inside', () => {
  const { cost, options, fire, rest, ctxSent } = probe();
  assert.equal(cost.observers, 0);
  assert.deepEqual(JSON.parse(JSON.stringify(options.pointermove)), [{ capture: true, once: true }]);
  fire('pointermove', { target: el('div', { bg: 'url("/inside.png")' }), clientX: 1, clientY: 1 });
  rest();
  assert.equal(ctxSent().at(-1).data.url, 'https://shop.example/inside.png');
});

test('a right press resolves at once and cancels the dwell; a left press does nothing', () => {
  const { fire, timers, ctxSent } = probe();
  fire('pointerover', { target: el('div'), clientX: 0, clientY: 0 });
  fire('mousedown', { button: 0, target: el('div', { bg: 'url("/l.png")' }), clientX: 0, clientY: 0 });
  assert.equal(ctxSent().length, 0);
  fire('mousedown', { button: 2, target: el('div', { bg: 'url("/r.png")' }), clientX: 0, clientY: 0 });
  assert.equal(ctxSent().length, 1);
  assert.equal(ctxSent()[0].data.url, 'https://shop.example/r.png');
  assert.equal(timers.size, 0, 'the pending dwell cannot overwrite the press');
});

test('contextmenu always reports, even when the dwell already sent the same find', () => {
  const tile = el('div', { bg: 'url("/same.png")' });
  const { fire, rest, ctxSent } = probe();
  fire('pointerover', { target: tile, clientX: 3, clientY: 4 });
  rest();
  fire('contextmenu', { target: tile, clientX: 3, clientY: 4 });
  assert.equal(ctxSent().length, 2);
  assert.deepEqual(JSON.parse(JSON.stringify(ctxSent()[1].point)), { x: 3, y: 4 });
});

// Players strip <video poster> once playback starts; the capture listener stamps it first.
test('a poster stamped at play survives the player stripping it', () => {
  const video = at(el('video', { videoWidth: 640, videoHeight: 360, paused: true, currentTime: 0, readyState: 0, poster: '/before.jpg' }), 0, 0, 640, 360);
  const { fire, resolve } = probe({ videos: [] });
  fire('play', { target: video });
  video.poster = '';
  video.ownerDocument = { defaultView: { parent: null } };
  const { data } = resolve(video);
  assert.equal(data.poster, 'https://shop.example/before.jpg');
});

test('the double-injection guard binds one set of listeners, not two', () => {
  const { listeners, sandbox } = probe();
  const before = listeners.contextmenu.length;
  for (const src of SOURCES) vm.runInContext(src, sandbox);
  assert.equal(listeners.contextmenu.length, before);
});
