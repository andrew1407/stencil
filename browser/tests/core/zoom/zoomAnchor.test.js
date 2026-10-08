// ZoomPan.viewAnchor / restoreAnchor (js/core/zoom/pan.js): a rotate or flip returns to the
// zoom it left and keeps the same picture point centred; a fitted picture refits instead.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ZoomPan } from '../../../js/core/zoom/pan.js';
import { installDom, ROOMY } from '../../helpers/zoomPanViewportDom.js';

const zoomedApp = (width, height) => {
  const classes = new Set();
  return {
    image: { width, height },
    canvas: { width, height, style: {}, classList: { add: (c) => classes.add(c), remove: (c) => classes.delete(c) } },
    storage: { save() {} },
    classes,
  };
};

const setup = (w, h) => {
  const vp = installDom({ ...ROOMY, containerBottom: 933 });
  Object.assign(vp, { clientWidth: 800, clientHeight: 400, scrollLeft: 0, scrollTop: 0 });
  const app = zoomedApp(w, h);
  return { vp, app, zp: new ZoomPan(app) };
};

test('viewAnchor reads the zoom and the image point under the viewport centre', () => {
  const { vp, app, zp } = setup(1000, 500);
  zp.setZoom(2);
  vp.scrollLeft = 600; vp.scrollTop = 300;
  assert.deepEqual(zp.viewAnchor(), { scale: 2, fit: false, x: 500, y: 250 });
  app.image = null;
  assert.equal(zp.viewAnchor(), null);
});

test('restoreAnchor keeps the zoom and centres the anchored point', () => {
  const { vp, app, zp } = setup(500, 1000);
  zp.restoreAnchor({ scale: 2, fit: false, x: 250, y: 500 });
  assert.equal(app.scale, 2);
  assert.equal(app.canvas.style.width, '1000px');
  assert.equal(vp.scrollLeft, 250 * 2 - 400);
  assert.equal(vp.scrollTop, 500 * 2 - 200);
});

test('a fitted anchor refits the turned picture instead of keeping the old fit scale', () => {
  const { app, zp } = setup(1000, 500);
  zp.fitToWindow();
  const anchor = zp.viewAnchor();
  assert.equal(anchor.fit, true);
  app.image = { width: 500, height: 1000 };
  Object.assign(app.canvas, { width: 500, height: 1000 });
  zp.restoreAnchor(anchor);
  assert.equal(app.scale, zp.fitScale());
});
