// content/ctxTarget.js — the right-click probe injected on <all_urls>. It is a classic
// IIFE content script (no exports, no imports), so it is run in a vm over a fabricated
// page: loading it IS arming it, and the listeners it binds are the surface under test.
//
// What it decides is what the context menu shows, so the rules that matter are the
// negative ones: a real <img> must never reveal the background group, and a normal page
// link must never reveal the menu at all.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { MSG } from '../src/lib/messages.js';

const SRC = readFileSync(fileURLToPath(new URL('../src/content/ctxTarget.js', import.meta.url)), 'utf8');
const PAGE = 'https://shop.example/gallery';

// ── A page-lite: exactly the surface the probe reaches for ──
const el = (tag, props = {}) => {
  const node = {
    tagName: tag.toUpperCase(), nodeType: 1, parentElement: null, attrs: {}, children: [],
    rect: { x: 0, y: 0, left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 },
    bg: 'none',
    getAttribute: (k) => (k in node.attrs ? node.attrs[k] : null),
    getBoundingClientRect: () => node.rect,
    // Tag selectors only, with any attribute filter ('a[href]') taken as read.
    matches: (sel) => sel.split(',').some((s) => s.trim().split('[')[0].toUpperCase() === node.tagName),
    closest(sel) {
      for (let n = node; n; n = n.parentElement) if (n.matches(sel)) return n;
      return null;
    },
    ownerDocument: null,
    ...props,
  };
  return node;
};
const at = (node, x, y, w, h) => {
  node.rect = { x, y, left: x, top: y, right: x + w, bottom: y + h, width: w, height: h };
  return node;
};
const chain = (...nodes) => {
  for (let i = 1; i < nodes.length; i++) nodes[i - 1].parentElement = nodes[i];
  return nodes[0];
};

// Load the probe over a page holding `videos`, with `stack` under the cursor point.
const probe = ({ videos = [], stack = [], canvas = null } = {}) => {
  const sent = [];
  const listeners = {};
  const document = {
    documentElement: el('html'),
    addEventListener: (t, fn) => { (listeners[t] ||= []).push(fn); },
    querySelectorAll: (sel) => (sel === 'video' ? videos : []),
    elementsFromPoint: () => stack,
    createElement: () => canvas || { getContext: () => ({ drawImage() {} }), toDataURL: () => 'data:image/jpeg;base64,FRAME' },
  };
  const sandbox = {
    window: { __proto__: null, devicePixelRatio: 2, parent: null },
    document,
    location: { href: PAGE },
    URL,
    Date,
    Math,
    getComputedStyle: (node) => ({ backgroundImage: node.bg || 'none' }),
    MutationObserver: class { observe() {} },
    chrome: { runtime: { sendMessage: (m, cb) => { sent.push(m); if (cb) cb(); }, lastError: null } },
  };
  sandbox.window.document = document;
  // topRect walks out through the frame chain, so a video has to know its window.
  for (const v of videos) v.ownerDocument = { defaultView: sandbox.window };
  vm.createContext(sandbox);
  vm.runInContext(SRC, sandbox);
  const fire = (type, ev) => { for (const fn of listeners[type] || []) fn(ev); };
  // The probe reports through sendMessage; the last CTX payload is its verdict. The
  // payload crosses the vm realm, so it is compared by value, not by prototype.
  const resolve = (target, x = 10, y = 10) => {
    fire('contextmenu', { target, clientX: x, clientY: y });
    const msg = sent.filter((m) => m.type === MSG.CTX).at(-1);
    return JSON.parse(JSON.stringify({ data: msg.data ?? null }));
  };
  return { sent, fire, resolve, listeners, sandbox };
};

test('loading the probe wakes the lazy worker, so the menu exists before the first click', () => {
  const { sent } = probe();
  assert.equal(sent[0].type, MSG.WAKE);
  // The mirror inside the content script must be the canonical table's spelling.
  assert.ok(SRC.includes(`WAKE: '${MSG.WAKE}'`) && SRC.includes(`CTX: '${MSG.CTX}'`));
});

// NEGATIVE: the native 'image' context already builds the menu for a real <img>. Reporting
// `url` would reveal the BACKGROUND group on top of it.
test('a real <img> reports imgUrl ONLY — never url, so no second menu group appears', () => {
  const img = el('img', { currentSrc: '/photo.png' });
  const { resolve } = probe();
  const msg = resolve(chain(el('span'), img));
  assert.deepEqual(msg.data, { imgUrl: 'https://shop.example/photo.png' });
});

test('an <svg><image> reads its href attribute, not its SVGAnimatedString src', () => {
  const image = el('image');
  image.attrs.href = '../icons/logo.svg';
  const { resolve } = probe();
  assert.deepEqual(resolve(image).data, { imgUrl: 'https://shop.example/icons/logo.svg' });
});

test('a background-image ancestor is resolved absolute, from the nearest one up', () => {
  const inner = el('span');
  const tile = el('div', { bg: 'url("tiles/bg.jpg")' });
  const { resolve } = probe();
  assert.deepEqual(resolve(chain(inner, tile)).data, { url: 'https://shop.example/tiles/bg.jpg' });
});

test('an image buried under a click-catcher overlay is found at the cursor point', () => {
  const overlay = el('div');
  const buried = el('img', { src: '/under.png' });
  const { resolve } = probe({ stack: [overlay, buried] });
  assert.deepEqual(resolve(overlay).data, { url: 'https://shop.example/under.png' });
});

// NEGATIVE: a link is the LAST resort, and only to an image file — otherwise every
// ordinary page link would reveal the menu.
test('a link straight to an image file is the last resort; any other link is not', () => {
  const link = el('a');
  link.attrs.href = '/downloads/shot.png';
  const { resolve } = probe();
  assert.deepEqual(resolve(link).data, { url: 'https://shop.example/downloads/shot.png' });
  const page = el('a');
  page.attrs.href = '/about';
  assert.equal(probe().resolve(page).data, null);
});

test('nothing grabbable under the cursor reports null, not a guess', () => {
  assert.equal(probe().resolve(el('p')).data, null);
});

// ── Video ──

test('a playing video reports its captured frame, its poster and its media URL', () => {
  const video = at(el('video', { videoWidth: 1920, videoHeight: 1080, paused: false, currentTime: 4, readyState: 4, poster: '/p.jpg', currentSrc: 'https://cdn.example/v.mp4' }), 0, 0, 640, 360);
  const { resolve } = probe({ videos: [video] });
  const { data } = resolve(video);
  assert.equal(data.video, true);
  assert.equal(data.url, 'data:image/jpeg;base64,FRAME');
  assert.equal(data.poster, 'https://shop.example/p.jpg');
  assert.equal(data.videoUrl, 'https://cdn.example/v.mp4');
});

// A video sitting on its poster draws frame 0 (commonly black), so the frame is refused
// and the screenshot route is described instead.
test('a video still on its poster asks for a screenshot crop rather than a black frame', () => {
  const video = at(el('video', { videoWidth: 1920, videoHeight: 1080, paused: true, currentTime: 0, readyState: 4, poster: '/p.jpg' }), 12, 30, 640, 360);
  const { data } = probe({ videos: [video] }).resolve(video);
  assert.equal(data.posterShown, true);
  assert.equal(data.url, undefined, 'no frame is offered');
  assert.deepEqual(data.rect, { x: 12, y: 30, width: 640, height: 360 });
  assert.equal(data.dpr, 2, 'the screenshot has to be cropped in device pixels');
});

test('a tainted canvas is not an error: the frame is simply refused', () => {
  const video = at(el('video', { videoWidth: 800, videoHeight: 600, paused: false, currentTime: 2, readyState: 4 }), 0, 0, 800, 600);
  const tainted = { getContext: () => ({ drawImage() {} }), toDataURL: () => { throw new Error('tainted'); } };
  const { data } = probe({ videos: [video], canvas: tainted }).resolve(video);
  assert.equal(data.video, true);
  assert.equal(data.url, undefined);
});

// Players lay a controls overlay over the <video>, so closest() misses it — and several
// videos can share one wrapper, where an ancestor query would pick the wrong one.
test('the video under an overlay is the SMALLEST box containing the cursor', () => {
  const big = at(el('video', { videoWidth: 1920, videoHeight: 1080, paused: true, currentTime: 0, readyState: 0, currentSrc: 'https://cdn.example/big.mp4' }), 0, 0, 1000, 800);
  const small = at(el('video', { videoWidth: 640, videoHeight: 360, paused: true, currentTime: 0, readyState: 0, currentSrc: 'https://cdn.example/small.mp4' }), 100, 100, 200, 120);
  const { data } = probe({ videos: [big, small] }).resolve(el('div'), 150, 140);
  assert.equal(data.videoUrl, 'https://cdn.example/small.mp4');
});

// ── Priming ──
// The menu group is revealed by UPDATING the menu, which races Chrome's render — so the
// probe resolves on hover too. Ten tiles of the same background must cost one message.

test('hovering resolves early, and repeats of the same find are deduped', () => {
  const tile = () => el('div', { bg: 'url("/hero.png")' });
  const { sent, fire } = probe();
  let clock = 0;
  const originalNow = Date.now;
  Date.now = () => (clock += 200);   // past the 150ms throttle, under the 1s same-element gate
  try {
    for (let i = 0; i < 10; i++) fire('pointerover', { target: tile(), clientX: i, clientY: 0 });
  } finally { Date.now = originalNow; }
  assert.equal(sent.filter((m) => m.type === MSG.CTX).length, 1, 'one message for ten identical tiles');
});

test('the double-injection guard binds one set of listeners, not two', () => {
  const { listeners, sandbox } = probe();
  const before = listeners.contextmenu.length;
  vm.runInContext(SRC, sandbox);
  assert.equal(listeners.contextmenu.length, before);
});
