// applyLaunchQuery inside a frame (js/console/launchQuery.js): a host page could dress the confirm
// buttons up as its own, so only the picture and the look apply — servers, the assistant, a
// server picture and the project fields are left out with one notice.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { boot, connected, decodeTo, texts, called, applyLaunchQuery } from '../helpers/launchQueryRig.js';

const inFrame = async (run) => {
  const had = Object.getOwnPropertyDescriptor(globalThis, 'window');
  const self = had?.value ?? {};
  Object.defineProperty(globalThis, 'window', { value: Object.assign(Object.create(self), { parent: {} }), configurable: true, writable: true });
  try { await run(); } finally {
    if (had) Object.defineProperty(globalThis, 'window', had); else delete globalThis.window;
  }
};

test('framed, a link keeps its picture and theme but drops connect, llm and project fields', async () => {
  await inFrame(async () => {
    const { app, stencil } = boot('?blank=white&theme=dark&desc=Hi', '#connect=https://srv.example&token=t&llm-url=https://evil.example', {
      confirm() { this.calls.push(['confirm']); return Promise.resolve(true); },
    }, connected(false));
    const decode = decodeTo(app);
    try { await applyLaunchQuery({ app, stencil }); } finally { decode.restore(); }
    assert.deepEqual(called(app, 'setThemeMode'), [['setThemeMode', 'dark']]);
    assert.equal(called(app, 'decoded').length, 1, 'the blank still opens');
    assert.equal(called(app, 'confirm').length, 0);
    assert.equal(called(app, 'connect').length, 0);
    assert.equal(called(app, 'setProjectDescription').length, 0);
    assert.notEqual(stencil.llm.baseUrl, 'https://evil.example');
    assert.deepEqual(texts(), ['Launch link: inside a frame, its servers, assistant and project fields are left out']);
  });
});

test('framed, a server picture is left out too; a plain picture link says nothing', async () => {
  await inFrame(async () => {
    const shared = boot('?server=https://srv.example&id=r1', '', {}, connected(true));
    await applyLaunchQuery(shared);
    assert.equal(called(shared.app, 'openRemoteProject').length, 0);
    assert.equal(texts().length, 1);

    const plain = boot('?blank=white');
    const decode = decodeTo(plain.app);
    try { await applyLaunchQuery(plain); } finally { decode.restore(); }
    assert.deepEqual(texts(), []);
  });
});
