// The points panel's fold STACKED under the canvas (animations/responsive.css, below 960px): nothing slides
// sideways, the table is its own picture, and the list folds its height on the slide's clock (user report).
import test from 'node:test';
import assert from 'node:assert';
import { installCoordFoldRig } from '../../helpers/coordFoldRig.js';

const rig = await installCoordFoldRig();
const { fold, panel, body, shut, frames, toggle } = rig;
const { PANEL_SLIDE_IN_MS, PANEL_SLIDE_OUT_MS, VEILED_CLASS } = fold;
test.mock.timers.enable({ apis: ['setTimeout'] });
const reset = (mode, open) => rig.reset((ms) => test.mock.timers.tick(ms), mode, open);
// Stacked on top of the rig's own answers (the OS reduced-motion preference stays rig.setReduced's).
const rigMedia = globalThis.matchMedia;
globalThis.matchMedia = (q) => ({ matches: q === '(max-width: 960px)' || !!rigMedia?.(q).matches });

test('stacked under the canvas nothing slides: the table is its own picture, unveiled', () => {
  reset('particles', true);
  toggle();
  assert.ok(shut() && !panel.classes.has(VEILED_CLASS));
  assert.ok(body.__dustHost?.__cloud.motes.every((m) => m.cut === 1), 'the table\'s grains, cut out of it');
  assert.equal(frames[0].length, 0, 'no speck waits on the table');
});

// Stacked, the list folded in one jump once its fade ended (user report): now its height runs on the slide's
// clock, and the folded box keeps an inline 0 so the late display flip never shows it whole.
test('stacked, the list folds and unfolds its height on the slide, held displayed and clipped meanwhile', () => {
  const runs = [];
  const shown = [];
  panel.scrollIntoView = (o) => shown.push(o);
  const run = { playState: 'finished', cancel() {}, finished: Promise.resolve() };
  body.animate = (keyframes, opts) => { runs.push({ keyframes, opts }); return run; };
  body.offsetHeight = 464;
  const hadCs = globalThis.getComputedStyle;
  globalThis.getComputedStyle = () => ({ getPropertyValue: () => ' ease-out ' });
  try {
    reset('particles', true);
    runs.length = 0;   // the reset's own toggle ran too
    toggle();
    assert.ok(shut() && panel.classes.has('coord-folding'), 'folding: the box is held displayed');
    assert.deepEqual(runs.at(-1).keyframes, [{ height: '464px' }, { height: '0px' }], 'the list runs from its height to 0');
    assert.deepEqual(runs.at(-1).opts, { duration: PANEL_SLIDE_OUT_MS, easing: 'ease-out' }, 'on the slide-out clock, the fold ease');
    assert.equal(body.style.height, '464px', 'pinned at its start until the first frame');
    test.mock.timers.tick(PANEL_SLIDE_OUT_MS - 1);
    assert.ok(panel.classes.has('coord-folding'), 'held for the whole slide, not half of it');
    test.mock.timers.tick(2);
    assert.ok(!panel.classes.has('coord-folding'));
    toggle();
    assert.ok(!shut());
    assert.deepEqual(runs.at(-1).keyframes, [{ height: '0px' }, { height: '464px' }], 'and back up from 0');
    assert.equal(runs.at(-1).opts.duration, PANEL_SLIDE_IN_MS);
    // Reduced motion: no run, the folded list simply holds 0.
    test.mock.timers.tick(2000);
    rig.setReduced(true);
    toggle();
    assert.equal(runs.length, 2, 'nothing runs under reduced motion');
    assert.equal(body.style.height, '0px', 'folded, the list holds an inline 0');
    toggle();
    assert.equal(body.style.height, '', 'open, the layout has it back');
    assert.deepEqual(shown, [{ block: 'nearest', behavior: 'instant' }],
      'and the page follows the opened list, at once under reduced motion; the eased opens follow as they land');
  } finally {
    delete body.animate; delete body.offsetHeight; delete panel.scrollIntoView; globalThis.getComputedStyle = hadCs;
  }
});
