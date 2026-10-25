// The three video-frame routes (src/background/frameCapture.js) over a recorded chrome.scripting:
// the in-page read targets the clicked frame and hands back a tainted video's src and time; the
// byte re-fetch runs that page-derived src through the guard before anything is fetched or injected.
import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { captureVideoFrameInTab, captureVideoFrameViaFetch } from '../../src/background/frameCapture.js';

let injected;
let fetched;
let answer;
const saved = { chrome: globalThis.chrome, fetch: globalThis.fetch };

beforeEach(() => {
  injected = [];
  fetched = [];
  answer = () => [{ result: null }];
  globalThis.chrome = {
    scripting: { executeScript: async (opts) => { injected.push(opts); return answer(opts); } },
  };
  globalThis.fetch = async (url, init) => {
    fetched.push({ url: String(url), redirect: init?.redirect });
    return new Response(new Uint8Array([0, 0, 0, 24, 102, 116, 121, 112]), { status: 200, headers: { 'content-type': 'video/mp4' } });
  };
});
afterEach(() => { globalThis.chrome = saved.chrome; globalThis.fetch = saved.fetch; });

test('the in-page read targets the clicked frame and passes the probe point and the size cap', async () => {
  answer = () => [{ result: { frame: 'data:image/jpeg;base64,AA' } }];
  assert.deepEqual(await captureVideoFrameInTab(4, 7, { x: 10, y: 20 }), { frame: 'data:image/jpeg;base64,AA' });
  assert.deepEqual(injected[0].target, { tabId: 4, frameIds: [7] });
  assert.deepEqual(injected[0].args, [10, 20, 1920]);
  await captureVideoFrameInTab(4, null, null);
  assert.deepEqual(injected[1].target, { tabId: 4 }, 'no frame id: the top frame');
  assert.deepEqual(injected[1].args, [null, null, 1920], 'no point: the page picks its largest video');
});

test('the in-page read hands a tainted video back as its src and time, and fails soft', async () => {
  answer = () => [{ result: { src: 'https://cdn.example.com/v.mp4', t: 3.5 } }];
  assert.deepEqual(await captureVideoFrameInTab(1, 0, { x: 1, y: 1 }), { src: 'https://cdn.example.com/v.mp4', t: 3.5 });
  assert.equal(await captureVideoFrameInTab(null, 0, null), null, 'no tab, no injection');
  answer = () => { throw new Error('Cannot access contents of the page'); };
  assert.equal(await captureVideoFrameInTab(1, 0, null), null);
  assert.equal(injected.length, 2);
});

test('the re-fetch refuses a page-derived src aimed inside the network: nothing fetched, nothing injected', async () => {
  for (const src of ['http://127.0.0.1/v.mp4', 'http://169.254.169.254/latest', 'http://[::1]/v.mp4', 'file:///etc/passwd']) {
    assert.equal(await captureVideoFrameViaFetch(2, 0, src, 1, 'https://videos.example.com/watch'), null, src);
  }
  assert.deepEqual(fetched, []);
  assert.deepEqual(injected, []);
});

test('the re-fetch of a public src reads it without following redirects and seeks the recorded time in the page', async () => {
  answer = () => [{ result: 'data:image/jpeg;base64,FRAME' }];
  const out = await captureVideoFrameViaFetch(2, 5, 'https://cdn.example.com/v.mp4', 4.25, 'https://videos.example.com/watch');
  assert.equal(out, 'data:image/jpeg;base64,FRAME');
  assert.deepEqual(fetched, [{ url: 'https://cdn.example.com/v.mp4', redirect: 'manual' }]);
  assert.deepEqual(injected[0].target, { tabId: 2, frameIds: [5] });
  assert.match(injected[0].args[0], /^data:video\/mp4;base64,/, 'the bytes ride into the page as a data URL');
  assert.deepEqual(injected[0].args.slice(1), [4.25, 1920]);
});

test('a refused or empty reply from the re-fetch is no frame, so the caller falls through to the next route', async () => {
  globalThis.fetch = async () => new Response('', { status: 403 });
  assert.equal(await captureVideoFrameViaFetch(2, 0, 'https://cdn.example.com/v.mp4', 0, ''), null);
  assert.equal(await captureVideoFrameViaFetch(2, 0, '', 0, ''), null);
  assert.equal(await captureVideoFrameViaFetch(null, 0, 'https://cdn.example.com/v.mp4', 0, ''), null);
  assert.deepEqual(injected, []);
});
