// ZoomPan.availContentHeight() (js/core/zoomPan.js): `below` is summed from the viewport's own
// column, never `.container`, whose box also encloses the coordinates panel.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ZoomPan } from '../js/core/zoomPan.js';
import { installDom, ROOMY, ROOMY_AVAIL, MIN_VIEWPORT_H, CONTAINER_PAD, BODY_PAD } from './helpers/zoomPanViewportDom.js';

test('uses the space from the viewport top to the window bottom, less its own column footer', () => {
  installDom({ ...ROOMY, containerBottom: 933 });
  assert.equal(new ZoomPan({}).availContentHeight(), ROOMY_AVAIL);
});

// The shell's appReveal entrance slides the app 8px down while the first measure runs, so the
// measure reads the LAID-OUT top, with the ancestor's translate taken off (user report).
test('a mid-flight translate on the shell does not shorten the frame', () => {
  const vp = installDom({ ...ROOMY, vpTop: ROOMY.vpTop + 8, vpBottom: ROOMY.vpBottom + 8, sectionBottom: ROOMY.sectionBottom + 8, containerBottom: 941,
                          containerTransform: 'matrix(1, 0, 0, 1, 0, 8)' });
  const zp = new ZoomPan({});
  assert.equal(zp.availContentHeight(), ROOMY_AVAIL, 'the same room as at rest');
  // …and the coordinates panel beside it measures the same way.
  zp.syncCoordPanelHeight();
  assert.equal(vp.panel.style.maxHeight, `${Math.floor(953 - 362 - (CONTAINER_PAD + BODY_PAD))}px`);
});

// `below` counts the .container's own 20px bottom padding too; taken as room the viewport has,
// the page ends up 20px taller than the window.
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

// Measuring `below` to the stretched column's bottom edge counts the slack under a capped
// viewport — a fixed point: avail equals the current height, so it can never grow again.
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
