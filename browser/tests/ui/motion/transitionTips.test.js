// Transition's tooltips (js/ui/motion/control/tips.js): a tip grows and fades up out of its control, and leaves
// by shrinking ITSELF (it stays laid out as it hides), so it keeps its own look; the canvas readout, hidden with
// display:none, leaves a copy by class and markup that removes itself; the keyframes beat .dust-driven's cancel.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDustPage, boxEl, rect } from '../../helpers/dustRig.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';

const { tipFallbackIn, tipFallbackOut, tipGrowIn, tipCopyAway, TIP_GROW_CLASS, TIP_SHRINK_CLASS } =
  await import('../../../js/ui/motion/control/tips.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');

const tipEl = () => {
  const el = boxEl(rect(100, 60, 140, 40));
  el.id = 'app-tooltip';
  return el;
};

test('a tip grows in, then shrinks itself out: no stripped copy that loses its look', (t) => {
  const p = installDustPage(t);
  setMotionPrefs({ mode: 'transition' });
  const el = tipEl();
  assert.ok(tipFallbackIn(el, { x: 120, y: 50 }) > 0, 'the grow reports its run');
  assert.ok(el.classList.contains(TIP_GROW_CLASS));
  const before = p.doc.body.children.length;
  assert.equal(tipFallbackOut(el, { x: 120, y: 50 }), true);
  assert.ok(el.classList.contains(TIP_SHRINK_CLASS), 'the tip itself shrinks');
  assert.ok(!el.classList.contains(TIP_GROW_CLASS), 'and its grow gives way');
  assert.equal(p.doc.body.children.length, before, 'no copy joins the page');
  tipFallbackIn(el, { x: 120, y: 50 });
  assert.ok(!el.classList.contains(TIP_SHRINK_CLASS), 'shown again mid-shrink: it grows from where it is');
});

test('the canvas readout leaves a copy of itself that removes itself once its shrink has run', (t) => {
  const p = installDustPage(t);
  setMotionPrefs({ mode: 'transition' });
  const el = boxEl(rect(300, 200, 120, 60));
  el.className = 'tooltip tip-grow-in';
  el.innerHTML = '<table class="tooltip-table"></table>';
  assert.equal(tipCopyAway(el), true);
  const copy = p.doc.body.children.at(-1);
  assert.match(copy.className, /tooltip/, 'styled by the class the readout wears');
  assert.ok(copy.className.includes(TIP_SHRINK_CLASS) && !copy.className.includes(TIP_GROW_CLASS));
  assert.equal(copy.innerHTML, el.innerHTML);
  assert.deepEqual([copy.style.left, copy.style.top, copy.style.display], ['300px', '200px', 'block']);
  for (const ms of p.delays()) p.fire(ms);
  assert.ok(!p.doc.body.children.includes(copy), 'gone once its run is over');
});

// Sliding's tip hid on its own dissolve while its picture poured home: two exits at once (user report).
test("Sliding's pour takes the box's own fade with it", (t) => {
  installDustPage(t, { Image: function Image() {} });
  setMotionPrefs({ mode: 'slide' });
  const el = tipEl();
  el.cloneNode = () => boxEl(rect(100, 60, 140, 40), { querySelectorAll: () => [] });   // the stand-in copy
  assert.equal(tipFallbackOut(el, { x: 120, y: 50 }), true, 'the pour plays');
  assert.ok(el.classList.contains('dust-driven'), 'and the box goes at once with the hide, under its picture');
  assert.ok(!el.classList.contains(TIP_SHRINK_CLASS));
});

test('outside Transition neither plays', (t) => {
  installDustPage(t);
  setMotionPrefs({ mode: 'particles' });
  const el = tipEl();
  assert.equal(tipGrowIn(el), false);
  assert.equal(tipCopyAway(el), false);
  assert.ok(!el.classList.contains(TIP_GROW_CLASS));
});

// A tip the dust once drove keeps .dust-driven for good, and its animation:none !important cancelled the grow:
// Transition's tip only faded (user report). Its own keyframes must outrank that, both ways.
test('the grow and the shrink outrank .dust-driven', () => {
  assert.match(ANIMATIONS_CSS, /\.dust-driven \{ animation: none !important; \}/, 'the cancel the rules must beat');
  assert.match(ANIMATIONS_CSS, /\.dust-driven\.tip-grow-in \{ animation: tipGrowIn [^}]*!important; \}/);
  assert.match(ANIMATIONS_CSS, /\.dust-driven\.tip-shrink-out \{ animation: tipShrinkOut [^}]*!important; \}/);
});
