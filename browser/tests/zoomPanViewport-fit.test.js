// ZoomPan.fitToWindow and clampPanelWidth (js/core/zoomPan.js): the fit is measured off the
// viewport box, and the panel width is one token every fallback reads.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { ZoomPan } from '../js/core/zoomPan.js';
import { LAYOUT_CSS, COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';
import { installDom, ROOMY, ROOMY_AVAIL } from './helpers/zoomPanViewportDom.js';


// fitToWindow() sizes the fit off the MEASURED availContentHeight(), not fixed window insets:
// with the toolbar expanded a guess runs ~240px generous and the fit clips (user report).

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

// The coordinates panel's width is PERSISTED, so a width dragged in a large window comes back
// into a small one and must be clamped, or the layout grows wider than the viewport.
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

// One number, five references — the panel, the fullscreen points drawer, that drawer's resizer
// rail and its off-screen transform: written out per site they drift apart.
test('the untouched panel width is one token, and every fallback reads it', () => {
  const layout = LAYOUT_CSS;
  const px = Number(/--coord-panel-default:\s*(\d+)px/.exec(layout)?.[1]);
  assert.ok(px >= 240 && px <= 640, `--coord-panel-default ${px}px sits inside the clamp range`);
  for (const [file, css] of [['layout.css', LAYOUT_CSS],
                            ['components.css', COMPONENTS_CSS], ['animations.css', ANIMATIONS_CSS]]) {
    assert.ok(!/var\(--coord-panel-width,\s*\d/.test(css),
      `${file}: a hard-coded fallback beside the token is how the two drifted apart`);
  }
});

test('the panel width is re-clamped when the window changes, keeping the preference', () => {
  const src = readFileSync(new URL('../js/utils/panelResizer.js', import.meta.url), 'utf8');
  const fn = src.slice(src.indexOf('export const wirePanelResizer'));
  assert.match(fn, /onWindowResize\(applyStored\)/,
    'a window narrowed after the drag re-clamps (via the shared resize coalescer)');
  // The STORED value is never rewritten by a clamp — only by a real drag — so the width
  // comes back when the window is big enough for it again.
  const stores = fn.match(/sessionStorage\.setItem/g) || [];
  assert.equal(stores.length, 1, 'only the drag persists a width');
  const css = LAYOUT_CSS;
  // Declarations only — the comment above this rule NAMES the trap it avoids, and matching
  // the prose would pass (or fail) for the wrong reason.
  const container = css.slice(css.indexOf('.container {'), css.indexOf('}', css.indexOf('.container {')))
    .replace(/\/\*[\s\S]*?\*\//g, '');
  // An auto cross-axis margin turns OFF flex stretch, which is what let this box size to
  // its content and grow past the window.
  assert.ok(!/margin:\s*0\s+auto/.test(container), '.container must not size to its content');
  assert.match(container, /min-width:\s*0/, 'and must be allowed to shrink');
});
