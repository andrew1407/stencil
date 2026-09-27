// content/ctxResolve.js — what the right-click probe resolves under the cursor, driven through
// the loaded probe's contextmenu. What it decides is what the context menu shows, so the rules
// that matter are the negative ones: a real <img> never reveals the background group, a page
// link never reveals the menu.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { el, at, chain, probe } from '../helpers/ctxProbeEnv.js';

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
  image.attrs.href = '../../icons/logo.svg';
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
