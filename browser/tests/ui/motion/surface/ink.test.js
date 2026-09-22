// A mark that paints no box of its own dusts in the SHAPE of its ink, the way the desktop
// photographs a label onto a transparent surface (controlReveal.cpp groupShot): the cells the
// words and the glyph miss fly nothing. Pinned: what the ink sheet draws, how the mask gates
// motes, and that a group with a painted child still fills its whole grid.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../../helpers/dom.js';
import { recordingCtx, argsOf } from '../../../helpers/recordingCtx.js';

class StubPath { constructor(d) { this.d = d; } }
globalThis.Path2D = StubPath;
globalThis.localStorage = { getItem: () => null, setItem: () => {}, removeItem: () => {} };

// The badge's real box and grid: reshapeGrid(MARK_COLS, MARK_ROWS, 186, 18, MARK_MOTE_PX).
const BADGE = { left: 0, top: 0, width: 186, height: 18 };
const COLS = 62;
const ROWS = 6;
const CELLS = COLS * ROWS;

// A 2D context whose read-back is whatever the test says is inked; everything else records.
const inkCtx = (ink) => {
  const { ctx, calls } = recordingCtx();
  ctx.getImageData = (_x, _y, w, h) => {
    const data = new Uint8ClampedArray(w * h * 4);
    for (let cy = 0; cy < h; cy++)
      for (let cx = 0; cx < w; cx++) data[(cy * w + cx) * 4 + 3] = Math.round(ink(cx, cy, w, h) * 255);
    return { data };
  };
  return { ctx, calls };
};

const cs = (over = {}) => ({ backgroundColor: 'transparent', color: 'rgb(20,20,20)',
                             borderTopStyle: 'none', borderTopWidth: '0px', fontStyle: 'normal',
                             fontWeight: '600', fontSize: '13px', fontFamily: 'Inter', ...over });

// The stub document plus the canvas double both sheets come out of.
const rig = (ink) => {
  const made = [];
  const doc = installDom({
    createElement: (tag) => {
      const el = createStubElement(tag);
      if (tag === 'canvas') { const c = inkCtx(ink); made.push(c); el.getContext = () => c.ctx; }
      return el;
    },
  }, {
    getComputedStyle: (node) => node.__cs || cs(),
    requestAnimationFrame: () => 1,
    cancelAnimationFrame: () => {},
    matchMedia: () => ({ matches: false }),
    Path2D: StubPath,
  });
  doc.createRange = () => ({
    node: null,
    selectNodeContents(n) { this.node = n; },
    getBoundingClientRect: () => ({ left: 8, top: 3, width: 170, height: 12 }),
  });
  return { doc, made };
};

// Rows 1..4 carry the words; every third column inside them is a stroke. 84 of 372 cells.
const TEXT_BAND = (cx, cy) => (cy >= 1 && cy <= 4 && cx % 3 === 0 ? 0.9 : 0);
const INKED = Math.ceil(COLS / 3) * 4;

const { inkAlpha, textInk, INK_FLOOR } = await import('../../../../js/ui/motion/surface/ink.js');
const { groupPainter } = await import('../../../../js/ui/motion/surface/painters.js');
const { revealControls, settleMark } = await import('../../../../js/ui/motion.js');
const { setMotionPrefs } = await import('../../../../js/ui/motion/motionPrefs.js');

const badge = (over = {}) => createStubElement('span', {
  getBoundingClientRect: () => BADGE,
  querySelectorAll: () => [],
  __cs: cs(),
  ...over,
});

test('the mask is one alpha per cell, read back off the sheet the ink was drawn on', () => {
  const { doc } = rig(TEXT_BAND);
  const mask = inkAlpha(badge(), COLS, ROWS);
  assert.ok(mask instanceof Float32Array);
  assert.equal(mask.length, CELLS);
  assert.ok(mask[0 * COLS + 0] < INK_FLOOR, 'the row above the words is bare paper');
  assert.ok(mask[2 * COLS + 3] >= INK_FLOOR, 'a stroke in the text band is ink');
  assert.ok(mask[2 * COLS + 4] < INK_FLOOR, 'the gap beside it is not');
  assert.equal(mask.filter((a) => a >= INK_FLOOR).length, INKED);
  doc.restore();
});

test('an unmeasurable mark hands back no mask at all, so its cloud is never thinned', () => {
  const { doc } = rig(() => 0);
  assert.equal(inkAlpha(badge(), COLS, ROWS), null, 'a blank sheet is a failed measurement');
  assert.equal(inkAlpha(badge({ getBoundingClientRect: () => ({ left: 0, top: 0, width: 0, height: 0 }) }),
                        COLS, ROWS), null, 'and so is a mark with no box');
  doc.restore();
  // No canvas at all (the node suite's default document): the same answer, not a throw.
  const plain = installDom({});
  assert.equal(inkAlpha(badge(), COLS, ROWS), null);
  plain.restore();
});

test('the sheet carries the words in their own style and the glyph out of its viewBox', () => {
  const { doc } = rig(TEXT_BAND);
  const { ctx, calls } = recordingCtx();
  const word = doc.createTextNode('Incognito — not saved');
  const glyph = createStubElement('svg', {
    getBoundingClientRect: () => ({ left: 12, top: 5, width: 13, height: 13 }),
    getAttribute: (k) => ({ viewBox: '0 0 24 24', 'stroke-width': '2' }[k] ?? null),
    querySelectorAll: () => [
      createStubElement('path', { getAttribute: (k) => (k === 'd' ? 'M2 12h20' : null) }),
      createStubElement('circle', {
        getAttribute: (k) => ({ cx: '6.5', cy: '15.5', r: '2.8' }[k] ?? null) }),
    ],
  });
  const el = badge();
  el.append(glyph, word);
  textInk(ctx, el, 0, 0, (n) => n.__cs || cs());

  assert.deepEqual(argsOf(calls, 'fillText'), [['Incognito — not saved', 8, 9]],
    'the run is drawn at the box the Range measured for it, centred in it');
  assert.ok(calls.some(([k, v]) => k === 'set:font' && v === 'normal 600 13px Inter'),
    'in the style of the element that holds the text, not the canvas default');
  const strokes = argsOf(calls, 'stroke').map(([p]) => p.d);
  assert.equal(strokes.length, 2, 'both shapes of the glyph stroke');
  assert.equal(strokes[0], 'M2 12h20');
  assert.match(strokes[1], /^M3.7 15.5a2.8 2.8 0 1 0 5.6 0a2.8 2.8 0 1 0 -5.6 0$/,
    'a circle becomes two arcs — Path2D takes path data, not SVG attributes');
  assert.deepEqual(argsOf(calls, 'scale'), [[13 / 24, 13 / 24]], 'the 24-grid scaled to the rendered glyph');
  assert.deepEqual(argsOf(calls, 'translate'), [[12, 5]]);
  doc.restore();
});

// The flight the incognito badges take: js/ui/toolbar/toolbar.js and
// js/ui/projects/window/projectTitle.js both hand a background-less span to revealControls.
const flyBadge = (el) => {
  setMotionPrefs({ mode: 'particles' });
  el.style.display = 'none';
  revealControls(el, true, 'flex');
  const motes = el.__dustHost?.__cloud?.motes ?? [];
  settleMark(el);
  return motes;
};

test('a text-only badge flies its ink, not its box', () => {
  const { doc } = rig(TEXT_BAND);
  const motes = flyBadge(badge());
  assert.equal(motes.length, INKED, 'one mote per inked cell, and none for the bare paper');
  assert.ok(motes.length < CELLS * 0.4,
    `the slab was ${CELLS} motes; the mark is ${motes.length}`);
  doc.restore();
});

test('…and the mask is the only reason: ink everywhere and the same badge fills its grid', () => {
  const { doc } = rig(() => 1);
  assert.equal(flyBadge(badge()).length, CELLS,
    'a cell is dropped for want of ink, never by the painter losing cells on its own');
  doc.restore();
});

test('a group with a painted child keeps the cloud it has — the mask is never consulted', () => {
  const { doc, made } = rig(TEXT_BAND);
  const child = createStubElement('button', {
    getBoundingClientRect: () => ({ left: 4, top: 2, right: 90, bottom: 16, width: 86, height: 14 }),
    __cs: cs({ backgroundColor: 'rgb(124,58,237)' }),
  });
  const group = badge({ querySelectorAll: () => [child] });
  assert.equal(flyBadge(group).length, CELLS, 'a real control group still dusts as its controls');
  assert.equal(made.length, 1, 'only the cloud canvas was made — no ink sheet was ever drawn');
  doc.restore();
});

test('a painted box of its own also keeps it, so a separator is untouched', () => {
  const { doc, made } = rig(TEXT_BAND);
  const sep = badge({ __cs: cs({ backgroundColor: 'rgb(200,160,60)' }) });
  assert.equal(typeof groupPainter(sep), 'function');
  assert.equal(flyBadge(sep).length, CELLS, '.sel-sep paints its own hairline: it is not line-art');
  assert.equal(made.length, 1);
  doc.restore();
});
