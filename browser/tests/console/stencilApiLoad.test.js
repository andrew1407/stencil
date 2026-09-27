// stencil.load and stencil.chat.swapSides (js/console/stencilApi.js): incognito adoption in
// place, a failed fetch leaving the editor alone, and the restamped transcripts.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installFetchStub } from '../helpers/fetchStub.js';
import { installImageDecode } from '../helpers/projectTransferRig.js';
import {
  createStencil, makeApp, called, chatTranscript, ctxAssistTranscript,
} from '../helpers/stencilApiRig.js';

// load(url, { incognito }) is llm-contract.md §10 openUrl, adopting in place: a fresh incognito
// session in the SAME tab, so the conversation driving it keeps the working image in front of it.
const stubFetch = (blob) => installFetchStub(blob instanceof Error ? blob : { blob }).restore;
// The real load flow (core/image/loadFlow.js) reads the fetched file; its decode lands a 4 × 3 picture.
const decodeTo = (app) => installImageDecode(() => { app.calls.push(['decoded']); app.image = { width: 4, height: 3 }; });

test('load({ incognito }) adopts incognito in place and loads here — no new tab', async () => {
  const app = makeApp();
  const stencil = createStencil(app);
  const restore = stubFetch(new Blob(['x'], { type: 'image/png' }));
  const decode = decodeTo(app);
  try {
    await stencil.load('https://pics.example/cat.png', { incognito: true });
  } finally { restore(); decode.restore(); }

  // adoptIncognitoHere reset the editor keeping the chat and flagged it, THEN the load read the file.
  const order = app.calls.map(([n]) => n).filter((n) => ['newTemporary', 'updateIncognitoUI', 'decoded'].includes(n));
  assert.deepEqual(order, ['newTemporary', 'updateIncognitoUI', 'decoded']);
  assert.deepEqual(called(app, 'newTemporary'), [['newTemporary', { keepChat: true }]]);
  assert.deepEqual(decode.reads(), ['cat.png']);
  assert.equal(app.imageSource, 'https://pics.example/cat.png');
  // An incognito load is never promoted to a saved project.
  assert.equal(called(app, 'promoteTemporaryToProject').length, 0);
  assert.equal(app.storage.incognito, true);
  assert.deepEqual(app.image, { width: 4, height: 3 });
});

test('load() without incognito never resets the editor', async () => {
  const app = makeApp();
  const stencil = createStencil(app);
  const restore = stubFetch(new Blob(['x'], { type: 'image/png' }));
  const decode = decodeTo(app);
  try {
    await stencil.load('https://pics.example/cat.png');
  } finally { restore(); decode.restore(); }
  assert.equal(called(app, 'newTemporary').length, 0, 'adoptIncognitoHere never ran');
  assert.equal(called(app, 'updateIncognitoUI').length, 0);
  assert.deepEqual(decode.reads(), ['cat.png']);
  assert.equal(app.storage.incognito, false);
});

test('a failed fetch leaves the editor alone — the adoption never runs', async () => {
  const app = makeApp();
  app.image = { width: 9, height: 9 };
  const stencil = createStencil(app);
  const restore = stubFetch(new Error('offline'));
  try {
    await assert.rejects(() => stencil.load('https://pics.example/cat.png', { incognito: true }), /offline/);
  } finally { restore(); }
  assert.equal(called(app, 'newTemporary').length, 0, 'adoptIncognitoHere never ran');
  assert.equal(called(app, 'updateIncognitoUI').length, 0);
  assert.equal(app.storage.incognito, false);
  assert.deepEqual(app.image, { width: 9, height: 9 });
});

test('chat.swapSides gets/sets the tab-session-only side and restamps both transcripts live', () => {
  const app = makeApp();
  const stencil = createStencil(app);
  assert.equal(stencil.chat.swapSides, false, 'default is normal, unswapped');
  stencil.chat.swapSides = true;
  assert.equal(stencil.chat.swapSides, true);
  assert.ok(chatTranscript.classes.has('chat-swapped'), 'the panel transcript restamped immediately');
  assert.ok(ctxAssistTranscript.classes.has('chat-swapped'), 'the ctx-assist transcript restamped too');
  stencil.chat.swapSides = false;
  assert.equal(stencil.chat.swapSides, false);
  assert.ok(!chatTranscript.classes.has('chat-swapped'));
  assert.ok(!ctxAssistTranscript.classes.has('chat-swapped'));
});
