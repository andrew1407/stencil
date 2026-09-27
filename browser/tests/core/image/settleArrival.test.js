// A decoded file lands through settleLoadedImage (js/core/image/settle.js) with the arrival the
// open route plays: the picture and its lines, from the drop point; an in-place load stays still.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

const doc = installDom({}, { requestAnimationFrame: () => 0, cancelAnimationFrame: () => {} });
const container = doc.register('canvas-container', createStubElement('div'));
const { settleLoadedImage } = await import('../../../js/core/image/settle.js');
const { ARRIVE_GLOW_CLASS } = await import('../../../js/ui/motion.js');

const rig = () => {
  const layerCalls = [];
  const overlay = { id: 'overlay' };
  const app = {
    image: {}, canvas: { width: 4, height: 3 }, rotationQuarters: 0, lines: [], pendingLines: null,
    imageModel: { defaultCropRect: () => ({ x: 0, y: 0, width: 4, height: 3 }), rebuildCroppedImage() {} },
    history: { reset() {} }, coordTable: { update() {} }, zoomPan: { fitToWindow() {} },
    renderer: { redraw() {}, layers: () => { layerCalls.push(1); return [app.canvas, overlay]; } },
    updateInfo() {}, updateButtons() {}, updateCoordStatus() {},
    storage: { save() {}, store: { getMeta: () => null } },
    tabs: { reportActive() {}, reportIncognito() {} },
  };
  return { app, layerCalls };
};

const file = () => new File([new Uint8Array([1])], 'img.png', { type: 'image/png' });

test('a loaded file arrives from its drop point, its lines riding with the picture', async () => {
  const { app, layerCalls } = rig();
  container.classList.remove(ARRIVE_GLOW_CLASS);
  await settleLoadedImage(app, file(), { from: { x: 40, y: 30 } }, { replaceInPlace: false });
  assert.equal(layerCalls.length, 1, 'the arrival took the renderer\'s layers');
  assert.ok(container.classList.contains(ARRIVE_GLOW_CLASS), 'and flew in from the drop point');
});

test('an in-place replace keeps the picture still', async () => {
  const { app, layerCalls } = rig();
  container.classList.remove(ARRIVE_GLOW_CLASS);
  await settleLoadedImage(app, file(), { landing: false, keepZoom: true },
    { replaceInPlace: true, keptLines: [] });
  assert.equal(layerCalls.length, 0);
  assert.ok(!container.classList.contains(ARRIVE_GLOW_CLASS));
});
