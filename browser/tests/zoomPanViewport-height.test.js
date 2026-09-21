// ZoomPan.syncViewportHeight / syncCoordPanelHeight (js/core/zoomPan.js): the frame takes the
// whole available height, and the panel is capped to the room below its own top.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ZoomPan } from '../js/core/zoom/zoomPan.js';
import { installDom, ROOMY, ROOMY_AVAIL, MIN_PANEL_H, CONTAINER_PAD, BODY_PAD } from './helpers/zoomPanViewportDom.js';


// syncViewportHeight: the cap is the available height, full stop, never a hug of the picture,
// which collapsed the frame to a strip (user report); layout.css centres the image in it.

const sized = (app) => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  new ZoomPan(app).syncViewportHeight();
  return parseFloat(vp.style.maxHeight);
};

test('the frame takes the whole available height whatever the picture is doing', () => {
  const withImage = (h, scale) => ({ image: { width: 100, height: h }, canvas: { width: 100, height: h, style: {} },
                                     scale, storage: { save() {} } });
  assert.equal(sized({}), ROOMY_AVAIL, 'no image at all');
  assert.equal(sized(withImage(40, 1)), ROOMY_AVAIL, 'a picture far shorter than the frame');
  assert.equal(sized(withImage(1587, 0.1)), ROOMY_AVAIL, 'zoomed out to a thumbnail');
  assert.equal(sized(withImage(1587, 0.32)), ROOMY_AVAIL, 'fitted');
  assert.equal(sized(withImage(1587, 8)), ROOMY_AVAIL, 'and zoomed far past it — the cap is what scrolls');
});

test('a cleared image does not leave the frame at the size the picture had', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  const app = { image: { width: 300, height: 200 }, canvas: { width: 300, height: 200, style: {} },
                scale: 1, storage: { save() {} } };
  const zp = new ZoomPan(app);
  zp.syncViewportHeight();
  app.image = null;                       // newEditor(): the picture is gone
  zp.syncViewportHeight();
  assert.equal(parseFloat(vp.style.maxHeight), ROOMY_AVAIL, 'the empty editor gets the room too');
});

test('every zoom step leaves the frame the same height', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  vp.clientWidth = 1061;
  const zp = new ZoomPan({ image: { width: 438, height: 619 },
                           canvas: { width: 438, height: 619, style: {} }, storage: { save() {} } });
  for (const s of [0.05, 0.5, 1, 4, 32]) {
    zp.setZoom(s, false);
    assert.equal(parseFloat(vp.style.maxHeight), ROOMY_AVAIL, `unchanged at ${s}×`);
  }
});

test('fullscreen keeps its own box — the in-flow rule stays out of it', () => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  globalThis.document.body.classList.contains = (c) => c === 'fullscreen-mode';
  new ZoomPan({}).syncViewportHeight();
  assert.equal(vp.style.maxHeight, undefined, 'components.css pins it to the window');
});

// syncCoordPanelHeight: the panel starts BELOW the toolbar, so its cap is measured rather than
// CSS's `max-height: calc(100vh - 40px)`, which assumes the top of the page.

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
