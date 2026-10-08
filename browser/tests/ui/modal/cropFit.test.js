// The crop window's size (js/ui/modal/cropFit.js): the picture fitted into its room, the window fitted
// around it inside the viewport, and the re-fit a resize asks for. The fake layout below reproduces
// the numbers a real browser gives this markup. Desktop twin: tests/dialogs/crop/cropDialogFit.headless.cpp.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { fitContain, previewBox, cropWindowSize, wireCropFit, STAGE_MIN, VIEW_MARGIN }
  from '../../../js/ui/modal/cropFit.js';

const CHROME = { width: 38, height: 166 };   // header, body padding, size line, one-row footer, borders

test('fitContain keeps the aspect, grows a small picture and answers nothing for no room', () => {
  assert.deepEqual(fitContain(4000, 3000, 800, 800), { width: 800, height: 600, scale: 0.2 });
  assert.deepEqual(fitContain(100, 50, 800, 600), { width: 800, height: 400, scale: 8 });
  assert.equal(fitContain(600, 4000, 800, 400).height, 400, 'a tall picture is held by the height');
  for (const args of [[0, 10, 10, 10], [10, 10, 0, 10], [10, 10, 10, -5]])
    assert.deepEqual(fitContain(...args), { width: 0, height: 0, scale: 0 });
});

test('previewBox is 96% less 60 by 82% less 180 of the viewport, floored at 760×540', () => {
  assert.deepEqual(previewBox({ width: 1920, height: 1080 }), { width: 1783, height: 706 });
  assert.deepEqual(previewBox({ width: 1280, height: 720 }), { width: 1169, height: 540 });
  assert.deepEqual(previewBox({ width: 800, height: 600 }), { width: 760, height: 540 });
});

test('the opening window fits the viewport less its margin for any aspect', () => {
  const at = (iw, ih, viewport, floorW = 687) => cropWindowSize({ iw, ih, chrome: CHROME, viewport, floorW });
  const laptop = { width: 1280, height: 720 };
  assert.deepEqual(at(4000, 3000, laptop), { width: 724, height: 680 }, 'a photo is held by the height');
  assert.deepEqual(at(64, 48, laptop), { width: 724, height: 680 }, 'a tiny picture grows to the same');
  assert.deepEqual(at(6000, 800, laptop), { width: 1207, height: 322 }, 'a panorama by the width');
  assert.deepEqual(at(600, 4000, { width: 1366, height: 768 }), { width: 687, height: 706 },
    'a tall portrait opens at the footer\'s one line');
  const viewports = [laptop, { width: 1366, height: 768 }, { width: 900, height: 500 }, { width: 420, height: 380 }];
  for (const viewport of viewports) {
    for (const [iw, ih] of [[4000, 3000], [600, 4000], [6000, 800], [64, 48]]) {
      const s = at(iw, ih, viewport);
      assert.ok(s.width <= viewport.width - 2 * VIEW_MARGIN && s.height <= viewport.height - 2 * VIEW_MARGIN,
        `${iw}x${ih} in ${viewport.width}x${viewport.height} opens at ${s.width}x${s.height}`);
    }
  }
  assert.deepEqual(cropWindowSize({ iw: 400, ih: 300, chrome: { width: 38, height: 900 }, viewport: laptop }),
    { width: 38, height: 680 }, 'chrome taller than the viewport is still capped by it');
});

// A crop window over a fake layout: the footer wraps under 700px, which costs the picture 24px.
const rig = ({ viewport = { width: 1280, height: 720 }, open = false } = {}) => {
  const chromeH = (w) => (w < 700 ? 190 : 166);
  const width = (el) => parseFloat(el.style.width) || 560;
  const box = createStubElement('div');
  Object.defineProperties(box, {
    offsetWidth: { get: () => width(box) },
    clientWidth: { get: () => width(box) - 2 },
    offsetHeight: { get: () => parseFloat(box.style.height) || 400 },
  });
  const footer = createStubElement('div');
  Object.defineProperty(footer, 'offsetWidth',
    { get: () => (footer.style.width === 'max-content' ? 685 : box.clientWidth) });
  const frame = createStubElement('div');
  Object.defineProperties(frame, {
    clientWidth: { get: () => width(box) - CHROME.width },
    clientHeight: { get: () => box.offsetHeight - chromeH(width(box)) },
  });
  const stage = createStubElement('div');
  const overlay = createStubElement('div');
  if (open) overlay.classList.add('modal-open');
  const listeners = {};
  globalThis.window = { innerWidth: viewport.width, innerHeight: viewport.height,
                        addEventListener: (t, fn) => { listeners[t] = fn; } };
  const scales = [];
  const fit = wireCropFit({ overlay, box, frame, stage, footer }, () => ({ iw: 4000, ih: 3000 }),
                          (s) => scales.push(s));
  return { box, frame, stage, overlay, footer, fit, scales, listeners };
};

test('fitWindow sizes a closed window around its picture, measured unseen, and fits the stage', () => {
  const { box, stage, overlay, footer, fit, scales } = rig();
  Object.assign(overlay.style, { display: '', visibility: '' });   // a real style's unset values
  assert.equal(fit.fitWindow(), true);
  assert.equal(box.style.width, '724px');
  assert.equal(box.style.height, '680px');
  assert.equal(box.style.maxHeight, `calc(100vh - ${2 * VIEW_MARGIN}px)`, 'its own ceiling over the shell\'s 760px');
  assert.equal(box.dataset.minH, String(190 + STAGE_MIN), 'the narrowest window\'s chrome over the picture\'s floor');
  assert.equal(overlay.style.display, '', 'the overlay is hidden again after the measurement');
  assert.equal(overlay.style.visibility, '');
  assert.equal(footer.style.width, '', 'the footer is released after its one-line measurement');
  assert.equal(parseFloat(stage.style.height), 514, 'the stage fills the free height');
  assert.ok(Math.abs(parseFloat(stage.style.width) - 4000 * 514 / 3000) < 1e-6, 'at the picture\'s aspect');
  assert.ok(Math.abs(scales.at(-1) - 514 / 3000) < 1e-9, 'and the box repaints at that scale');
});

test('a resize of the free room re-fits the stage; no room leaves it be', () => {
  const { box, stage, fit, scales } = rig({ open: true });
  fit.fitWindow();
  box.style.width = '500px';
  box.style.height = '450px';
  assert.equal(fit.fitStage(), true);
  assert.equal(parseFloat(stage.style.height), 450 - 190, 'a narrower window wraps its footer; the picture yields');
  assert.ok(Math.abs(scales.at(-1) - 260 / 3000) < 1e-9);
  box.style.height = '190px';
  const before = stage.style.width;
  assert.equal(fit.fitStage(), false, 'no room: nothing to fit');
  assert.equal(stage.style.width, before);
});

test('a viewport resize re-fits a window at its own size, and a held size only once it no longer fits', () => {
  const { box, fit, listeners } = rig({ open: true });
  fit.fitWindow();
  window.innerWidth = 900;
  window.innerHeight = 600;
  listeners.resize();
  assert.equal(box.style.height, '560px', 'its own size follows the viewport');
  box.dataset.userSized = '1';
  box.style.translate = '40px 0px';
  box.style.width = '600px';
  box.getBoundingClientRect = () => ({ left: 150, top: 20, right: 750, bottom: 470 });
  listeners.resize();
  assert.equal(box.style.width, '600px', 'a held size that still fits stays');
  assert.equal(box.dataset.userSized, '1');
  box.getBoundingClientRect = () => ({ left: 350, top: 20, right: 950, bottom: 470 });
  listeners.resize();
  assert.equal(box.dataset.userSized, undefined, 'past the viewport it is let go');
  assert.equal(box.style.translate, '', 're-centred');
  assert.equal(box.style.width, '687px', 'and re-fitted as on open');
});
