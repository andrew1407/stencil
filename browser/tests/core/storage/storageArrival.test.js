// The arrival on the storage routes (js/core/storage/storage.js), on a real Storage over the stub
// page: an open plays the dust once its image is decoded and the saved scroll is in place, in the
// same tick; a peer's edit synced in from another tab repaints without it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountStorage } from '../../helpers/storageRig.js';
import { ASSEMBLING_CLASS } from '../../../js/ui/motion.js';

// The decode lands the moment src is set: onload is always assigned first.
class InstantImage {
  set src(v) { this.url = v; this.onload?.(); }
  get src() { return this.url; }
}

const rigWithDecode = (t) => {
  const prior = globalThis.Image;
  globalThis.Image = InstantImage;
  t.after(() => { globalThis.Image = prior; });
  const r = mountStorage(t, { image: false, app: {
    imageModel: { defaultCropRect: () => ({ x: 0, y: 0, width: 200, height: 100 }), rebuildCroppedImage() {} },
    updateCoordStatus() {},
  } });
  Object.assign(r.app.zoomPan, { setZoom(z) { this.zoom = z; }, fitToWindow() {} });
  // The arrival hands its layers over at the call: the scroll it saw is the scroll it snapshots.
  const arrivals = [];
  const layers = r.app.renderer.layers;
  r.app.renderer.layers = () => {
    arrivals.push([r.viewport.scrollLeft, r.viewport.scrollTop]);
    return layers();
  };
  return { ...r, arrivals };
};

const saved = (image) => ({ image, layout: { zoom: 2, scrollLeft: 35, scrollTop: 47, lines: [] } });

test('opening a saved project arrives, after its saved scroll and in the decode\'s own tick', (t) => {
  const r = rigWithDecode(t);
  r.storage.activeId = 'p1';
  r.storage.loadPayloadIntoApp(saved('data:image/png;base64,AAAA'), { landing: true });
  assert.deepEqual(r.arrivals, [[35, 47]], 'one arrival, over the slice the user will see');
  assert.ok(r.viewport.classList.contains(ASSEMBLING_CLASS), 'the canvas waits behind its motes');
  assert.equal(r.app.zoomPan.zoom, 2);
});

test('a project switched away mid-decode never arrives', (t) => {
  const r = rigWithDecode(t);
  r.storage.activeId = 'p1';
  const decoding = { onload: null, src: '' };
  globalThis.Image = class { constructor() { return decoding; } };
  r.storage.loadPayloadIntoApp(saved('data:image/png;base64,AAAA'), { landing: true });
  r.storage.activeId = 'p2';
  decoding.onload();
  assert.deepEqual(r.arrivals, []);
});

test('a peer\'s edit synced in from another tab repaints without the arrival', (t) => {
  const r = rigWithDecode(t);
  r.storage.store.upsert({ id: 'p1', name: 'peer' }, saved('data:image/png;base64,BBBB'));
  r.storage.activeId = 'p1';
  r.app.imageDataUrl = 'data:image/png;base64,AAAA';
  r.storage.syncActiveFromStorage();
  assert.equal(r.app.imageDataUrl, 'data:image/png;base64,BBBB', 'the peer\'s picture was loaded');
  assert.deepEqual(r.arrivals, [], 'and it did not dissolve in under the reader');
  assert.ok(!r.viewport.classList.contains(ASSEMBLING_CLASS));
});
