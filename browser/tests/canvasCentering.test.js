// The canvas is CENTRED in its viewport whenever the picture is smaller than the frame
// (css/layout.css) — and the zoom math goes through that offset (js/core/zoomPan.js).
//
// The bug these lock down: the canvas container was an inline-block in a block scrollport,
// so a zoomed-out picture flowed to the START edge and sat against the left of the frame
// with a wide band of empty ground beside it (user report, with a screenshot).
//
// The fix is the auto-margin idiom, NOT `text-align: center` / `justify-content: center`:
// those centre an OVERFLOWING child too, which puts its top-left corner out of the scroll
// range for good. Auto margins take the free space when there is some and collapse to 0
// when there isn't, so a zoomed-in canvas still scrolls to all four of its edges.
//
// Centring moves the image origin off the SCROLL origin, so every viewport→image
// conversion (zoom focal points) has to subtract it — that is canvasOrigin(), and the
// second half of this suite is its arithmetic.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { ZoomPan, canvasOrigin } from '../js/core/zoomPan.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from './helpers/css.js';

// ── The CSS contract ─────────────────────────────────────────────────────────
const layoutCss = LAYOUT_CSS;
const componentsCss = COMPONENTS_CSS;
// The DECLARATIONS of a rule: comments are stripped, so prose about the traps below
// (which every one of these blocks carries) is never mistaken for a declaration.
const blockOf = (css, sel) => {
  const at = css.indexOf(`\n${sel} {`);
  assert.ok(at >= 0, `no rule for ${sel}`);
  return css.slice(at, css.indexOf('\n}', at)).replace(/\/\*[\s\S]*?\*\//g, '');
};

test('the viewport is a flex COLUMN — the sticky incognito frame stays above the picture', () => {
  const vp = blockOf(layoutCss, '.canvas-viewport');
  assert.match(vp, /display: flex/);
  assert.match(vp, /flex-direction: column/,
    'a row would lay the incognito frame BESIDE the canvas instead of over the viewport');
});

// The frame is FULL HEIGHT at every zoom (ZoomPan.syncViewportHeight caps it at the
// available height, it does not hug the picture), so what fills the column up to that cap
// is the flex rule here. Lose it and a small picture shrink-wraps the frame again — the
// short horizontal strip in the user's screenshot.
test('the viewport fills its column up to the cap — the frame never shrink-wraps', () => {
  const vp = blockOf(layoutCss, '.canvas-viewport');
  assert.match(vp, /flex: 1 1 auto/, 'flex-grow is what takes the whole available height');
  assert.match(vp, /min-height: 0/, '…and a tall canvas must still be allowed to scroll inside it');
  assert.ok(!/height:\s*(auto|fit-content|max-content)/.test(vp), 'no content-sized height');
});

test('the canvas centres with auto margins, never with justify/align/text-align', () => {
  const c = blockOf(layoutCss, '.canvas-container');
  assert.match(c, /margin: auto/, 'auto margins are what centre it');
  assert.match(c, /flex: none/, 'a shrinkable flex item would squash the picture instead of scrolling');
  assert.ok(!/width:/.test(c), 'the container still shrink-wraps the canvas');
  // The traps: any of these centres an OVERFLOWING canvas too, and its top-left corner
  // then cannot be reached by scrolling at all.
  for (const trap of [/justify-content/, /align-items/, /place-content/, /text-align/]) {
    assert.ok(!trap.test(blockOf(layoutCss, '.canvas-viewport')), `viewport must not use ${trap}`);
    assert.ok(!trap.test(c), `container must not use ${trap}`);
  }
});

test('fullscreen shares the same idiom — no centring keywords of its own', () => {
  const fs = blockOf(componentsCss, 'body.fullscreen-mode .canvas-viewport');
  assert.match(fs, /display: flex/);
  // `align-items/justify-content: safe center` used to live here and centred the incognito
  // FRAME as a second row item beside the picture.
  assert.ok(!/justify-content|align-items/.test(fs),
    'fullscreen centres through the shared auto margins, not its own keywords');
});

test('the incognito frame is pinned at its size as a flex item', () => {
  assert.match(blockOf(componentsCss, '.incognito-frame'), /flex: none/,
    'an overflowing canvas would otherwise shrink the frame and it would stop tracing the viewport');
});

// ── canvasOrigin() and the zoom math ─────────────────────────────────────────
// A viewport modelled the way the browser lays this out: the canvas is centred by auto
// margins while it fits, the scroll offset is clamped to the real range, and the content
// box is exactly the canvas. Fullscreen, because there the frame is a fixed box on both
// axes (outside it the height hugs the picture) — the centring math is the same one.
const installViewport = ({ vpW = 1000, vpH = 700, imgW = 600, imgH = 400, scale = 1 } = {}) => {
  const canvas = { width: imgW, height: imgH, style: {}, classList: { add() {}, remove() {} } };
  const shown = (side) => parseFloat(canvas.style[side]) || 0;
  const clamp = (v, max) => Math.max(0, Math.min(v, Math.max(0, max)));
  const vp = {
    clientWidth: vpW, clientHeight: vpH, style: {}, sl: 0, st: 0,
    get scrollWidth() { return Math.max(vpW, shown('width')); },
    get scrollHeight() { return Math.max(vpH, shown('height')); },
    get scrollLeft() { return this.sl; },
    set scrollLeft(v) { this.sl = clamp(v, this.scrollWidth - vpW); },
    get scrollTop() { return this.st; },
    set scrollTop(v) { this.st = clamp(v, this.scrollHeight - vpH); },
  };
  // `margin: auto`, as the browser resolves it: half the free space, nothing when negative.
  const container = {
    get offsetLeft() { return Math.max(0, (vpW - shown('width')) / 2); },
    get offsetTop() { return Math.max(0, (vpH - shown('height')) / 2); },
  };
  globalThis.document = {
    getElementById: (id) => (id === 'canvas-viewport' ? vp : id === 'canvas-container' ? container : null),
    querySelectorAll: () => [],
    body: { classList: { contains: (c) => c === 'fullscreen-mode' } },
    activeElement: null,
  };
  globalThis.window = { innerWidth: vpW, innerHeight: vpH };
  globalThis.getComputedStyle = () => ({ borderTopWidth: '0px', borderBottomWidth: '0px',
                                         paddingTop: '0px', paddingBottom: '0px' });
  // Run the zoom animation straight to its final frame: the landing place is what matters.
  globalThis.performance = { now: () => 0 };
  globalThis.requestAnimationFrame = (cb) => { cb(1e6); return 1; };
  globalThis.cancelAnimationFrame = () => {};
  const app = { image: { width: imgW, height: imgH }, canvas, scale, storage: { save() {} } };
  const zp = new ZoomPan(app);
  zp.setZoom(scale, false);            // lay the canvas out at the starting zoom
  return { app, vp, zp };
};

// Where an image point sits inside the frame right now (0 = the frame's left/top edge).
const seenAt = (app, vp, x, y) => ({
  x: canvasOrigin().x + x * app.scale - vp.scrollLeft,
  y: canvasOrigin().y + y * app.scale - vp.scrollTop,
});

test('canvasOrigin: half the free space while the picture fits, 0 once it overflows', () => {
  const { app, vp, zp } = installViewport();
  assert.deepEqual(canvasOrigin(), { x: 200, y: 150 }, '(1000-600)/2, (700-400)/2');
  zp.setZoom(3, false);
  assert.deepEqual(canvasOrigin(), { x: 0, y: 0 }, 'an overflowing canvas has no margin left');
  assert.equal(vp.scrollWidth, 1800);
  assert.equal(app.scale, 3);
});

test('canvasOrigin: no container (or no DOM at all) is 0, never a throw', () => {
  installViewport();
  globalThis.document.getElementById = () => null;
  assert.deepEqual(canvasOrigin(), { x: 0, y: 0 });
});

test('originAt predicts, for a zoom not on screen yet, what the margins will do', () => {
  const { zp } = installViewport();
  for (const s of [0.25, 1, 1.6, 3]) {
    const predicted = zp.originAt(s);
    zp.setZoom(s, false);
    assert.deepEqual(predicted, canvasOrigin(), `originAt(${s}) must match the laid-out margin`);
  }
});

test('setZoom keeps renderedScale — what is ON SCREEN — in step with the logical scale', () => {
  // Left stale from an older animated zoom, the next focal zoom measures its start from a
  // scale the canvas no longer has and lands somewhere else entirely.
  const { app, zp } = installViewport();
  zp.zoomAroundCenter(2);
  assert.equal(app.renderedScale, 2);
  zp.setZoom(0.5, false);
  assert.equal(app.renderedScale, 0.5, 'a non-animated zoom is on screen too');
});

test('zooming in from a centred picture keeps the IMAGE centre in the middle of the frame', () => {
  const { app, vp, zp } = installViewport();
  zp.zoomAroundCenter(3);
  // Read off the centring margin, the middle of the frame is the middle of the picture —
  // not (scroll + clientWidth/2)/scale, which would answer with a point 200px off.
  assert.deepEqual(seenAt(app, vp, 300, 200), { x: 500, y: 350 }, 'image centre at frame centre');
  assert.equal(vp.scrollLeft, 400, '(1800 - 1000) / 2');
  assert.equal(vp.scrollTop, 250, '(1200 - 700) / 2');
});

test('zooming back out below the frame lands centred, at scroll 0 on both axes', () => {
  const { app, vp, zp } = installViewport({ scale: 3 });
  vp.scrollLeft = 400; vp.scrollTop = 250;
  zp.zoomAroundCenter(1);
  assert.equal(vp.scrollLeft, 0);
  assert.equal(vp.scrollTop, 0);
  assert.deepEqual(canvasOrigin(), { x: 200, y: 150 }, 'the margins are what centre it now');
  assert.deepEqual(seenAt(app, vp, 300, 200), { x: 500, y: 350 });
});

test('a zoom round trip does not drift', () => {
  const { app, vp, zp } = installViewport();
  const before = seenAt(app, vp, 300, 200);
  for (const s of [4, 0.5, 2, 1]) zp.zoomAroundCenter(s);
  assert.deepEqual(seenAt(app, vp, 300, 200), before);
});

test('a scrolled-in canvas still reaches its top-left corner', () => {
  const { app, vp, zp } = installViewport();
  zp.zoomAroundCenter(3);
  vp.scrollLeft = 0; vp.scrollTop = 0;
  // Centring must never move the corner out of reach — the whole reason for auto margins.
  assert.deepEqual(seenAt(app, vp, 0, 0), { x: 0, y: 0 });
  vp.scrollLeft = 1e6; vp.scrollTop = 1e6;
  assert.deepEqual(seenAt(app, vp, 600, 400), { x: 1000, y: 700 }, '…and its bottom-right one');
});

// ── A frame taller than the picture ──────────────────────────────────────────
// The frame used to hug the image, so a vertical margin barely existed; now it is full
// height at every zoom and the picture floats in the middle of it. These lock the axis
// the old sizing rule hid: a zoom that overflows sideways while still fitting top-to-bottom.

test('a picture shorter than the frame stays vertically centred at every zoom', () => {
  const { app, vp, zp } = installViewport({ vpH: 700, imgW: 600, imgH: 200 });
  assert.deepEqual(canvasOrigin(), { x: 200, y: 250 }, 'centred on both axes to start');
  for (const s of [0.25, 1, 2, 3]) {
    const predicted = zp.originAt(s);
    zp.setZoom(s, false);
    assert.deepEqual(canvasOrigin(), predicted, `originAt(${s}) must match the laid-out margin`);
    assert.equal(canvasOrigin().y, Math.max(0, (700 - 200 * s) / 2), 'half the free height');
    assert.equal(vp.scrollTop, 0, 'nothing to scroll while it fits');
  }
  assert.equal(app.scale, 3);
});

test('zooming past the frame WIDTH while it still fits in height keeps the centre put', () => {
  const { app, vp, zp } = installViewport({ vpH: 700, imgW: 600, imgH: 200 });
  zp.zoomAroundCenter(3);                        // 1800×600: overflows x, still fits y
  assert.deepEqual(canvasOrigin(), { x: 0, y: 50 }, 'x margin gone, y margin halved');
  assert.equal(vp.scrollLeft, 400, '(1800 - 1000) / 2');
  assert.equal(vp.scrollTop, 0, 'a picture that fits vertically has no scroll to take');
  assert.deepEqual(seenAt(app, vp, 300, 100), { x: 500, y: 350 }, 'image centre at frame centre');
});

test('zoomToImagePoint pins the focal pixel through the centring margins', () => {
  const { app, vp, zp } = installViewport();
  const before = seenAt(app, vp, 450, 300);
  assert.deepEqual(before, { x: 650, y: 450 }, 'origin + point, with the picture centred');
  zp.zoomToImagePoint(3, 450, 300);
  // Without the origin term the focal point would be derived 200px (x) / 150px (y) of
  // screen off, and the zoom would land somewhere else entirely.
  assert.deepEqual(seenAt(app, vp, 450, 300), before);
});

// ── The wheel zoom rides the same margins ────────────────────────────────────
// controlsBinder's Ctrl+wheel zoom writes the scroll offset itself, frame by frame. It used
// to leave the centring margin out ("they are 0 whenever a scroll offset can be") — true
// only while the frame HUGGED the picture. In a full-height frame the vertical margin is
// real, so the pixel under the cursor would slide the moment a zoom crossed into overflow.
test('the wheel zoom pins the cursor through the centring margins (source pin)', () => {
  const src = readFileSync(new URL('../js/ui/bindings/smoothZoom.js', import.meta.url), 'utf8');
  const fn = src.slice(src.indexOf('const runSmoothZoom'), src.indexOf("document.addEventListener('wheel'"));
  assert.equal((fn.match(/zoomPan\.originAt\(/g) || []).length, 2,
    'both the per-frame write and the final snap go through originAt');
  assert.ok(!/style\.maxHeight/.test(fn), 'and it no longer resizes the frame per frame');
});

// Sizing is syncViewportHeight's job alone (utils/viewportMetrics.js) now: zoomAroundCenter
// used to write its own hugging height, then read clientHeight out of the box it just changed.
test('only syncViewportHeight sizes the frame (source pin)', () => {
  const src = readFileSync(new URL('../js/utils/viewportMetrics.js', import.meta.url), 'utf8');
  const writes = src.match(/vp\.style\.(max|min)Height\s*=/g) || [];
  assert.equal(writes.length, 1, 'one writer, inside syncViewportHeight');
  assert.ok(!/viewportMaxHeightPx/.test(src), 'the image-hugging cap is gone');
});

// Fullscreen owns the box while it is on (components.css pins it to the window) and hands
// it back on the way out — through the same rule, not a height of its own.
test('leaving fullscreen restores the frame through syncViewportHeight (source pin)', () => {
  const src = readFileSync(new URL('../js/ui/fullscreenLayer.js', import.meta.url), 'utf8');
  assert.match(src, /zoomPan\.syncViewportHeight\(\)/, 'the exit path re-measures');
  // The exit flight is animated: measured as it starts, the toolbar rows are still coming
  // back and the frame lands ~60px too tall (a permanent page scrollbar).
  assert.match(src, /setTimeout\(\(\) => app\.zoomPan\.syncViewportHeight\(\), FLIP_MS/,
    '…and again once the flight has landed');
});
