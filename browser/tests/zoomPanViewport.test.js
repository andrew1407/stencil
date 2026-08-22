// Regression tests for ZoomPan.availContentHeight() (js/core/zoomPan.js) — the height
// the canvas viewport is allowed to take.
//
// The bug these lock down: `below` (what sits under the viewport) used to be measured
// off `.container`, whose box also encloses the COORDINATES PANEL sitting beside the
// canvas. A long point list — a traced outline is ~40 points — made that panel taller
// than the window, so `below` exceeded the window height, the subtraction went negative
// and the viewport clamped to its 120px floor: a thin strip with ~700px free beneath it.
// The measurement now comes off the viewport's own column (.canvas-section).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { ZoomPan } from '../js/core/zoomPan.js';

const MIN_VIEWPORT_H = 120;
const MIN_PANEL_H = 120;

/**
 * Build the ancestor chain availContentHeight() walks:
 *   <html> > <body> > .container > .canvas-section > #canvas-viewport
 * `sectionBottom` is where the viewport's own column ends (status line + drop hint
 * below it); `containerBottom` is where the whole shell ends — which the tall
 * coordinates panel can push far past the window bottom.
 *
 * The chain is modelled in full because availContentHeight() sums the bottom
 * padding/border/margin of EVERY ancestor up to <body>, not just <body>'s: the
 * .container's own bottom padding sits under the canvas column too.
 */
const CONTAINER_PAD = 20;
const BODY_PAD = 16;

const installDom = ({ innerHeight, vpTop, vpBottom, sectionBottom, containerBottom, vpBorder = 2,
                      panelTop = vpTop, footerH = sectionBottom - vpBottom }) => {
  const rect = (bottom) => ({ getBoundingClientRect: () => ({ bottom }) });
  const html = { className: 'html', parentElement: null };
  const body = { className: 'body', parentElement: html, classList: { contains: () => false } };
  const container = { ...rect(containerBottom), className: 'container', parentElement: body };
  const section = { ...rect(sectionBottom), className: 'canvas-section', parentElement: container };
  // The coordinates panel: the OTHER column that can outgrow the window. It sits beside the
  // canvas (same parent chain), starting at the same top the viewport does.
  const panel = { className: 'coordinates-panel', parentElement: container, style: {},
                  getBoundingClientRect: () => ({ top: panelTop }) };
  // The rows under the viewport inside its column (status line + drop hint), modelled as the
  // siblings the measurement actually sums — NOT as the column's bottom edge: the column is a
  // stretched flex box, so slack under a capped viewport must not read as occupied space.
  const footer = {
    className: 'coord-status', parentElement: section, nextElementSibling: null,
    getBoundingClientRect: () => ({ height: footerH }),
  };
  const viewport = {
    getBoundingClientRect: () => ({ top: vpTop, bottom: vpBottom, height: vpBottom - vpTop }),
    closest: (sel) => (sel === '.canvas-section' ? section : sel === '.container' ? container : null),
    parentElement: section,
    nextElementSibling: footer,
    style: {},
  };
  globalThis.window = { innerHeight, innerWidth: 1728 };
  globalThis.document = {
    documentElement: html,
    body,
    getElementById: (id) => (id === 'canvas-viewport' ? viewport : id === 'coord-panel' ? panel : null),
    querySelectorAll: () => [],       // the zoom-% inputs setZoom() writes back to
    activeElement: null,
  };
  // The viewport is a border-box element with a real frame (#canvas-viewport in style.css).
  // Everything else reports only the bottom inset that matters to the walk.
  const inset = (paddingBottom) => ({ paddingBottom, borderBottomWidth: '0px', marginBottom: '0px',
                                      marginTop: '0px', display: 'block' });
  globalThis.getComputedStyle = (el) => {
    if (el === viewport) {
      return { borderTopWidth: `${vpBorder}px`, borderBottomWidth: `${vpBorder}px`, paddingTop: '0px', paddingBottom: '0px' };
    }
    if (el === container) return inset(`${CONTAINER_PAD}px`);
    if (el === body) return inset(`${BODY_PAD}px`);
    return inset('0px');
  };
  viewport.panel = panel;
  return viewport;
};

// A roomy window: viewport starts 362px down, its column ends 95px below it.
const ROOMY = { innerHeight: 953, vpTop: 362, vpBottom: 838, sectionBottom: 933 };
// 953 - 362 - ((933 - 838) + 20 container pad + 16 body pad)
const ROOMY_AVAIL = 460;

test('uses the space from the viewport top to the window bottom, less its own column footer', () => {
  installDom({ ...ROOMY, containerBottom: 933 });
  assert.equal(new ZoomPan({}).availContentHeight(), ROOMY_AVAIL);
});

// The bug this locks down: `below` counted only <body>'s bottom padding, so the
// .container's own 20px bottom padding was room the viewport thought it had. The page
// ended up 20px taller than the window — a permanent ~15px scrollbar at 100% zoom.
test('counts the bottom padding of EVERY ancestor, not just body', () => {
  installDom({ ...ROOMY, containerBottom: 933 });
  const avail = new ZoomPan({}).availContentHeight();
  // Growing to `avail` must leave the container's and body's padding still on screen.
  const pageBottom = ROOMY.vpTop + avail + (ROOMY.sectionBottom - ROOMY.vpBottom) + CONTAINER_PAD + BODY_PAD;
  assert.ok(pageBottom <= ROOMY.innerHeight, `page would be ${pageBottom}px in a ${ROOMY.innerHeight}px window`);
  assert.equal(avail, ROOMY.innerHeight - ROOMY.vpTop - 95 - CONTAINER_PAD - BODY_PAD);
});

test('a tall coordinates panel beside the canvas does NOT shrink the viewport', () => {
  // Same geometry, but the sibling panel drags .container 636px past the window bottom —
  // which is exactly what a ~40-point traced outline does.
  installDom({ ...ROOMY, containerBottom: 1569 });
  const tall = new ZoomPan({}).availContentHeight();
  assert.equal(tall, ROOMY_AVAIL, 'height must be independent of the coordinates panel');
  assert.notEqual(tall, MIN_VIEWPORT_H, 'must not collapse to the floor');
});

// The bug this locks down: the column is a stretched flex box now (layout.css fills the
// window), so with the viewport capped to hug an image the leftover slack sits UNDER it.
// Measuring `below` to the column's bottom edge counted that slack as occupied space — a
// fixed point: avail came back equal to the current height, so a window resize (or a zoom
// step) could never grow the viewport again.
test('slack under a capped viewport is not mistaken for occupied space', () => {
  // The footer rows are unchanged (95px); the COLUMN is stretched 300px past them.
  installDom({ ...ROOMY, sectionBottom: 1233, containerBottom: 1233, footerH: 95 });
  assert.equal(new ZoomPan({}).availContentHeight(), ROOMY_AVAIL);
});

test('the floor still applies when the window really is too short', () => {
  installDom({ innerHeight: 200, vpTop: 362, vpBottom: 400, sectionBottom: 495, containerBottom: 495 });
  assert.equal(new ZoomPan({}).availContentHeight(), MIN_VIEWPORT_H);
});

test('fullscreen takes the whole window height', () => {
  installDom({ ...ROOMY, containerBottom: 1569 });
  globalThis.document.body.classList.contains = (c) => c === 'fullscreen-mode';
  assert.equal(new ZoomPan({}).availContentHeight(), 953);
});

// ── syncCoordPanelHeight ─────────────────────────────────────────────────────
// The bug these lock down: the panel's cap lived in CSS as `max-height: calc(100vh - 40px)`,
// which assumes it starts at the top of the page. It starts BELOW the toolbar, so a traced
// outline's ~40 rows ran a few hundred px past the window bottom and the page scrolled.

test('caps the coordinates panel to the room below its own top, not to 100vh', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  new ZoomPan({}).syncCoordPanelHeight();
  // 953 - 362 (its own top) - 20 container pad - 16 body pad = 555
  assert.equal(vp.panel.style.maxHeight, '555px');
  // The old CSS rule would have allowed 953 - 40 = 913, i.e. 358px past the window.
  assert.ok(parseFloat(vp.panel.style.maxHeight) < 953 - 40);
});

test('the capped panel cannot push the page past the window bottom', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  new ZoomPan({}).syncCoordPanelHeight();
  const bottom = ROOMY.vpTop + parseFloat(vp.panel.style.maxHeight) + CONTAINER_PAD + BODY_PAD;
  assert.ok(bottom <= ROOMY.innerHeight, `panel would reach ${bottom}px in a ${ROOMY.innerHeight}px window`);
});

// A top-docked chat slides body's padding down, moving the panel's top with it — the cap
// has to follow, which is why it is measured rather than written once in CSS.
test('follows the panel down when a top dock pushes it', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933, panelTop: 662 });
  new ZoomPan({}).syncCoordPanelHeight();
  assert.equal(vp.panel.style.maxHeight, '255px', '953 - 662 - 20 - 16');
});

test('never collapses below the panel floor on a tiny window', () => {
  const vp = installDom({ innerHeight: 300, vpTop: 280, vpBottom: 300, sectionBottom: 300, containerBottom: 300 });
  new ZoomPan({}).syncCoordPanelHeight();
  assert.equal(vp.panel.style.maxHeight, `${MIN_PANEL_H}px`);
});

test('leaves the panel alone in fullscreen (sized by the fullscreen layer)', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  globalThis.document.body.classList.contains = (c) => c === 'fullscreen-mode';
  new ZoomPan({}).syncCoordPanelHeight();
  assert.equal(vp.panel.style.maxHeight, undefined);
});

// ── fitToWindow ──────────────────────────────────────────────────────────────
// The bug these lock down: fitToWindow() sized the fit off fixed window insets
// (innerWidth-420 / innerHeight-220) while the viewport is clamped to the MEASURED
// availContentHeight(). With the toolbar expanded the guess is ~240px too generous, so
// "fit" picked a scale taller than the viewport and the bottom of the image — a rabbit's
// paws, in the report — sat clipped behind a scrollbar immediately after fitting.

const fitApp = (width, height) => ({
  image: { width, height },
  canvas: { width, height, style: {} },
  storage: { save() {} },
});

test('fit uses the measured viewport box, so the image is never clipped', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  vp.clientWidth = 1061;
  const app = fitApp(438, 619);          // the reported portrait image
  const zp = new ZoomPan(app);
  zp.fitToWindow();

  // availContentHeight() is 460; the old guess said 953-220=733 and fit at 100%.
  assert.equal(zp.availContentHeight(), ROOMY_AVAIL);
  assert.equal(app.scale, 0.73);         // floor((460 - 4px frame)/619 * 100)/100
  // The scaled image fits the viewport's CONTENT box — max-height less its 2px frame —
  // which is the comparison the scrollbar actually makes.
  const contentH = parseFloat(vp.style.maxHeight) - zp.viewportChromeY();
  assert.ok(app.canvas.height * app.scale <= contentH, 'fitted image must not overflow');
});

test('fit is bounded by width when the image is wide', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  vp.clientWidth = 600;
  const app = fitApp(2000, 400);
  new ZoomPan(app).fitToWindow();
  assert.equal(app.scale, 0.3);          // floor(600/2000 * 100)/100
});

test('fit never upscales a small image past 100%', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  vp.clientWidth = 1061;
  const app = fitApp(120, 90);
  new ZoomPan(app).fitToWindow();
  assert.equal(app.scale, 1);
});

test('fit falls back to the window inset when the viewport has no width yet', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  vp.clientWidth = 0;                    // pre-layout: measurement is not available
  const app = fitApp(4000, 100);
  new ZoomPan(app).fitToWindow();
  assert.equal(app.scale, 0.32);         // floor((1728-420)/4000 * 100)/100
});

// ── The coordinates panel may never push the page past the window ───────────
// Its width is PERSISTED, so a panel dragged wide in a large window comes back into a
// small one. Restored unclamped it made the layout wider than the viewport: the toolbar's
// right-hand sections ran off the edge and the page took a horizontal scrollbar.
test('clampPanelWidth: never leaves the canvas less than its minimum', async () => {
  const { clampPanelWidth, MIN_CANVAS_WIDTH } = await import('../js/utils.js');
  // Room for it → the stored preference is honoured exactly.
  assert.equal(clampPanelWidth(640, 1990), 640);
  assert.equal(clampPanelWidth(400, 1990), 400);
  // The regression: 640 restored into a window that cannot fit it beside the canvas.
  assert.equal(clampPanelWidth(640, 900), 900 - MIN_CANVAS_WIDTH);
  assert.ok(clampPanelWidth(640, 900) + MIN_CANVAS_WIDTH <= 900, 'the canvas keeps its minimum');
  // The 0.7 window fraction and the 640 ceiling still apply…
  assert.equal(clampPanelWidth(9999, 800), Math.min(640, Math.round(800 * 0.7), 800 - MIN_CANVAS_WIDTH));
  // …and the 240 floor wins on a very narrow window, where something has to give.
  assert.equal(clampPanelWidth(400, 300), 240);
  assert.equal(clampPanelWidth(10, 1990), 240);
});

test('the panel width is re-clamped when the window changes, keeping the preference', () => {
  const src = readFileSync(new URL('../js/utils.js', import.meta.url), 'utf8');
  const fn = src.slice(src.indexOf('export const wirePanelResizer'), src.indexOf('// ── Notification balloon'));
  assert.match(fn, /window\.addEventListener\('resize', applyStored\)/,
    'a window narrowed after the drag re-clamps');
  // The STORED value is never rewritten by a clamp — only by a real drag — so the width
  // comes back when the window is big enough for it again.
  const stores = fn.match(/localStorage\.setItem/g) || [];
  assert.equal(stores.length, 1, 'only the drag persists a width');
  const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
  // Declarations only — the comment above this rule NAMES the trap it avoids, and matching
  // the prose would pass (or fail) for the wrong reason.
  const container = css.slice(css.indexOf('.container {'), css.indexOf('}', css.indexOf('.container {')))
    .replace(/\/\*[\s\S]*?\*\//g, '');
  // An auto cross-axis margin turns OFF flex stretch, which is what let this box size to
  // its content and grow past the window.
  assert.ok(!/margin:\s*0\s+auto/.test(container), '.container must not size to its content');
  assert.match(container, /min-width:\s*0/, 'and must be allowed to shrink');
});
