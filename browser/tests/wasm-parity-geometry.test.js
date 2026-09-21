// Parity for the wasm core's POINT and CROP ops (js/wasm/stencilCore.js against the JS reference):
// the in/out array and CropRect out-pointer marshalling. Split from wasm-parity.test.js.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../js/core/abi/stencilCore.js';
import {
  cropAspectJS, centeredCropJS, resizeCropFromCornerJS, moveCropClampedJS, scaleCropCenteredJS,
  swapCropOrientationJS, cropResizeScaleJS, cropChangeJS, isAlbumOrientationJS, rotateCropRectQuarterJS
} from '../js/core/parse/cropGeometry.js';

// js/wasm/stencilCore.js is a generated, gitignored artifact, present only after the Emscripten build, so the
// suite skips when it is missing: the other suites already cover the JS reference path it mirrors.
const MODULE_BUILT = existsSync(fileURLToPath(new URL('../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

before(async () => {
  if (!MODULE_BUILT) return; // nothing to load; wtest already skipped the suite
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

wtest('rotatePoints + boundingBoxCenter: wasm matches JS rotation (in/out array marshalling)', () => {
  const boundingBoxCenter = core.op('boundingBoxCenter');
  const rotatePoints = core.op('rotatePoints');
  const pts = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
  assert.deepStrictEqual(boundingBoxCenter(pts), { x: 5, y: 5 });
  // JS reference rotation about (5,5) by 90°.
  const ang = Math.PI / 2;
  const cos = Math.cos(ang), sin = Math.sin(ang);
  const expect = pts.map(p => ({ x: 5 + (p.x - 5) * cos - (p.y - 5) * sin, y: 5 + (p.x - 5) * sin + (p.y - 5) * cos }));
  const got = pts.map(p => ({ ...p }));
  rotatePoints(got, 5, 5, ang);
  got.forEach((p, i) => {
    assert.ok(Math.abs(p.x - expect[i].x) < 1e-9 && Math.abs(p.y - expect[i].y) < 1e-9, `point ${i}`);
  });
});

wtest('flipPoints: wasm matches JS reflection about the bbox centre (int flag marshalling)', () => {
  const flipPoints = core.op('flipPoints');
  const pts = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
  const cx = 5, cy = 5;   // bbox centre of the square
  // Horizontal: x' = 2cx - x (y untouched); vertical: y' = 2cy - y (x untouched).
  const gotH = pts.map(p => ({ ...p }));
  flipPoints(gotH, true, cx, cy);
  gotH.forEach((p, i) => assert.ok(Math.abs(p.x - (2 * cx - pts[i].x)) < 1e-9 && Math.abs(p.y - pts[i].y) < 1e-9, `h point ${i}`));
  const gotV = pts.map(p => ({ ...p }));
  flipPoints(gotV, false, cx, cy);
  gotV.forEach((p, i) => assert.ok(Math.abs(p.x - pts[i].x) < 1e-9 && Math.abs(p.y - (2 * cy - pts[i].y)) < 1e-9, `v point ${i}`));
});

wtest('crop geometry: wasm matches JS reference (CropRect out-pointer marshalling)', () => {
  const [isAlbum, cropAspect, centeredCrop, resizeCorner, moveCrop, resizeScale, cropChange,
         scaleCrop, swapCrop, rotateCrop] = ['isAlbumOrientation', 'cropAspect', 'centeredCrop',
    'resizeCropFromCorner', 'moveCropClamped', 'cropResizeScale', 'cropChange',
    'scaleCropCentered', 'swapCropOrientation', 'rotateCropRectQuarter'].map((n) => core.op(n));
  const A3W = 29.7, A3H = 42.0;
  const rectClose = (a, b) => ['x', 'y', 'width', 'height'].forEach(k =>
    assert.ok(Math.abs(a[k] - b[k]) < 1e-9, `${k}: ${a[k]} ≈ ${b[k]}`));

  assert.strictEqual(isAlbum(200, 100), isAlbumOrientationJS(200, 100));
  assert.ok(Math.abs(cropAspect(A3W, A3H, true) - cropAspectJS(A3W, A3H, true)) < 1e-9);
  rectClose(centeredCrop(100, 200, cropAspectJS(A3W, A3H, false)), centeredCropJS(100, 200, cropAspectJS(A3W, A3H, false)));

  const cur = { x: 10, y: 10, width: 100, height: 70 };
  const aspect = cropAspectJS(A3W, A3H, true);
  rectClose(resizeCorner(cur, 2, 5000, 5000, aspect, 200, 200, 16), resizeCropFromCornerJS(cur, 2, 5000, 5000, aspect, 200, 200, 16));
  rectClose(moveCrop(cur, 9999, 0, 500, 500), moveCropClampedJS(cur, 9999, 0, 500, 500));
  assert.ok(Math.abs(resizeScale(100, 250) - cropResizeScaleJS(100, 250)) < 1e-9);
  const centred = { x: 60, y: 60, width: 80, height: 80 };
  rectClose(scaleCrop(centred, 1.5, 1, 200, 200), scaleCropCenteredJS(centred, 1.5, 1, 200, 200));   // grow, clamps at nearer edge
  rectClose(scaleCrop(centred, 0.5, 1, 200, 200), scaleCropCenteredJS(centred, 0.5, 1, 200, 200));   // shrink from centre
  rectClose(scaleCrop(centred, 100, 1, 200, 200), scaleCropCenteredJS(centred, 100, 1, 200, 200));   // over-grow → capped
  const framed = { x: 100, y: 40, width: 80, height: 160 }, none = { x: 0, y: 0, width: 0, height: 0 };
  const wide = centeredCropJS(2880, 2037, A3H / A3W);
  rectClose(swapCrop(framed, 2, 400, 400), swapCropOrientationJS(framed, 2, 400, 400));   // fits either way — a plain swap
  rectClose(swapCrop(wide, A3W / A3H, 2880, 2037), swapCropOrientationJS(wide, A3W / A3H, 2880, 2037));   // spills → shrinks about the centre
  rectClose(swapCrop(none, 1, 200, 100), swapCropOrientationJS(none, 1, 200, 100));   // no rect yet → centeredCrop

  const portrait = { x: 0, y: 0, width: 100, height: 141 };
  const album = { x: 0, y: 0, width: 141, height: 100 };
  assert.deepStrictEqual(cropChange(portrait, album), cropChangeJS(portrait, album));

  const r = { x: 10, y: 20, width: 80, height: 40 };
  rectClose(rotateCrop(r, 200, 100, true), rotateCropRectQuarterJS(r, 200, 100, true));
  rectClose(rotateCrop(r, 200, 100, false), rotateCropRectQuarterJS(r, 200, 100, false));
});
