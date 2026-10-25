// What a frame capture decodes it also lets go of: the worker closes every ImageBitmap it draws
// from (a screenshot, an oversized hand-off), and a video the injected reader makes is emptied once
// it has answered, since a detached <video> keeps downloading its src.
import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { captureFrameFromScreenshot, captureVideoFrameInTab, captureVideoFrameViaFetch } from '../../src/background/frameCapture.js';
import { openEditorTab } from '../../src/lib/menu/editorLaunch.js';
import { installDom, stubDoc, stubEl } from '../helpers/domStub.js';

let bitmaps;
let restore;
let failDraw;

class Canvas {
  constructor(w, h) { this.width = w; this.height = h; }
  getContext() { return { drawImage: () => { if (failDraw) throw new Error('draw failed'); } }; }
  async convertToBlob() { return new Blob([new Uint8Array(10)], { type: 'image/jpeg' }); }
}

beforeEach(() => {
  bitmaps = [];
  failDraw = false;
  restore = installDom({
    createImageBitmap: async () => {
      const bmp = { width: 4000, height: 3000, closed: false, close() { this.closed = true; } };
      bitmaps.push(bmp);
      return bmp;
    },
    OffscreenCanvas: Canvas,
    chrome: {
      tabs: { captureVisibleTab: async () => 'data:image/png;base64,AAAA', create: async (o) => ({ url: o.url }) },
      storage: { sync: { get: async (defaults) => defaults } },
    },
  });
});
afterEach(() => restore());

test('the screenshot crop closes its bitmap, drawn or not', async () => {
  assert.match(await captureFrameFromScreenshot(1, { x: 0, y: 0, width: 50, height: 40 }, 2), /^data:image\/jpeg;base64,/);
  failDraw = true;
  await assert.rejects(captureFrameFromScreenshot(1, { x: 0, y: 0, width: 50, height: 40 }));
  assert.deepEqual(bitmaps.map((b) => b.closed), [true, true]);
});

test('an oversized hand-off closes the bitmap it re-encodes from', async () => {
  const dataUrl = `data:image/png;base64,${'A'.repeat(2_000_000)}`;
  await openEditorTab({ dataUrl, name: 'big.png', incognito: true });
  assert.equal(bitmaps.length, 1);
  assert.equal(bitmaps[0].closed, true);
});

// The injected reader runs against a stub page; `made` collects the videos it creates.
const pageWith = (pageVideo) => {
  const made = [];
  const video = () => {
    const v = stubEl('video', { loads: 0, load() { v.loads++; } });
    v.removeAttribute = (k) => { if (k === 'src') v.src = ''; };
    made.push(v);
    return v;
  };
  const canvas = () => stubEl('canvas', { getContext: () => ({ drawImage() { throw new Error('tainted'); } }) });
  const undo = installDom({
    document: stubDoc({
      createElement: (t) => (t === 'video' ? video() : canvas()),
      querySelectorAll: () => [pageVideo],
    }),
  });
  return { made, undo };
};
const settle = () => new Promise((r) => setImmediate(r));

test('the CORS re-read empties its video once it has answered', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const pageVideo = stubEl('video', {
    videoWidth: 640, videoHeight: 360, readyState: 4, paused: false, currentTime: 3, currentSrc: 'https://cdn.example/v.mp4',
    rect: { left: 0, top: 0, right: 640, bottom: 360, width: 640, height: 360 },
  });
  const { made, undo } = pageWith(pageVideo);
  let run;
  globalThis.chrome.scripting = { executeScript: async ({ func, args }) => { run = func(...args); return [{ result: null }]; } };
  try {
    await captureVideoFrameInTab(1, 0, { x: 10, y: 10 });
    assert.equal(made.length, 1);
    made[0].fire('error');
    assert.deepEqual(await run, { src: 'https://cdn.example/v.mp4', t: 3 });
    assert.equal(made[0].src, '', 'the answered video still holds its src');
    assert.equal(made[0].loads, 1, 'the emptied video was never reloaded, so its fetch runs on');
  } finally { undo(); }
});

test('the byte re-read empties its video and revokes its blob once it has answered', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { made, undo } = pageWith(null);
  const savedFetch = globalThis.fetch;
  globalThis.fetch = async (url) => (String(url).startsWith('data:')
    ? savedFetch(url)
    : new Response(new Uint8Array([0, 0, 0, 24, 102, 116, 121, 112]), { status: 200, headers: { 'content-type': 'video/mp4' } }));
  let run;
  globalThis.chrome.scripting = { executeScript: async ({ func, args }) => { run = func(...args); return [{ result: null }]; } };
  try {
    await captureVideoFrameViaFetch(1, 0, 'https://cdn.example/v.mp4', 2, '');
    for (let i = 0; i < 5 && !made.length; i++) await settle();
    made[0].fire('error');
    assert.equal(await run, null);
    assert.equal(made[0].src, '');
    assert.equal(made[0].loads, 1);
  } finally { globalThis.fetch = savedFetch; undo(); }
});
