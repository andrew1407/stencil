// The canvas is CENTRED in its viewport whenever the picture is smaller than the frame
// (css/layout.css), and the zoom math goes through that offset (js/core/zoomPan.js). The idiom is
// auto margins, never `text-align: center` / `justify-content: center`, which centre an OVERFLOWING
// child too and put its top-left corner out of scroll range for good. Centring moves the image
// origin off the SCROLL origin, so every viewport→image conversion subtracts canvasOrigin().

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ZoomPan, canvasOrigin } from '../js/core/zoom/zoomPan.js';
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

// The frame is FULL HEIGHT at every zoom (ZoomPan.syncViewportHeight caps it at the available height
// rather than hugging the picture), and this flex rule is what fills the column to that cap.
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
