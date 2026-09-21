// The two folding surfaces (toolbar rows, points panel): foldBox reads the box they are about
// to reach, with the transitions off. Split from surfaceDust.test.js.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { motionSource } from './helpers/motionSource.js';

import { foldBox, FOLD_INSTANT_CLASS } from '../js/ui/motion.js';
import { ANIMATIONS_CSS } from './helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const animCss = ANIMATIONS_CSS;
const motionJs = motionSource();
const toolbarJs = read('../js/ui/toolbar/toolbar.js');
const mainContentJs = read('../js/ui/mainContent.js');


// The two folding surfaces have no opener icon and CSS owns their whole reveal, so at the moment
// the toggle flips they are still at the box they are LEAVING: foldBox supplies the box.

// A fake element/scope pair: the box it reports depends on whether `cls` is set, exactly
// as the real fold's does.
const foldStub = (cls, openBox, shutBox = { width: 0, height: 0, left: 0, top: 0 }) => {
  const classes = new Set([cls]);
  const reads = [];
  const el = {
    classList: {
      add: (c) => classes.add(c), remove: (c) => classes.delete(c),
      contains: (c) => classes.has(c),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
    },
    getBoundingClientRect: () => {
      const box = classes.has(cls) ? shutBox : openBox;
      reads.push({ box, instant: classes.has(FOLD_INSTANT_CLASS) });
      return box;
    },
  };
  return { el, classes, reads };
};

test('foldBox reads the OPEN box, with the transitions off, and puts the state back', () => {
  const open = { left: 0, top: 0, width: 900, height: 160 };
  const { el, classes, reads } = foldStub('hidden', open);
  const box = foldBox(el, el, 'hidden', false, FOLD_INSTANT_CLASS);
  assert.deepEqual(box, { left: 0, top: 0, width: 900, height: 160 },
    'the box dusted is the one the fold is about to reach, not the collapsed one');
  // Every read happened with the fold's easing suppressed — a transitioned read hands
  // back the box it is leaving, which is the whole bug this exists for.
  assert.ok(reads.length >= 2 && reads.every((r) => r.instant), 'measured with motion off');
  // …and nothing survives the round trip: same state in, same state out.
  assert.ok(classes.has('hidden'), 'the collapsed state is restored');
  assert.ok(!classes.has(FOLD_INSTANT_CLASS), 'the escape hatch is dropped again');
});

test('foldBox declines an unmeasurable box, and never throws on a stub', () => {
  const { el } = foldStub('hidden', { left: 0, top: 0, width: 4, height: 4 });
  assert.equal(foldBox(el, el, 'hidden', false, FOLD_INSTANT_CLASS), null);
  assert.equal(foldBox(null, null, 'hidden', false, FOLD_INSTANT_CLASS), null);
  assert.equal(foldBox({}, {}, 'hidden', false, FOLD_INSTANT_CLASS), null);
});

test('a folding surface hands its box in — the live rect is the wrong one', () => {
  // playSurface/surfaceDust/disintegrate all take the override, or a fold dusts over
  // a zero-height box and the flight silently declines.
  assert.match(motionJs, /export const surfaceIn = \(el, point, \{[^}]*\bms = SURFACE_IN_MS\b[^}]*\bbox = null\b[^}]*\} = \{\}\) =>/);
  assert.match(motionJs, /export const surfaceOut = \(el, point, \{[^}]*\bms = SURFACE_OUT_MS\b[^}]*\bbox = null\b[^}]*\} = \{\}\) =>/);
  const dust = motionJs.slice(motionJs.indexOf('const surfaceDust ='));
  assert.match(dust, /const r = box \|\| el\.getBoundingClientRect\(\);/);
  const dis = motionJs.slice(motionJs.indexOf('export function disintegrate'));
  assert.match(dis, /const r = box \|\| el\.getBoundingClientRect\(\);/);
  // The suppression class is real CSS, on both folds and their fading children.
  const instant = animCss.slice(animCss.indexOf('#controls-body.fold-instant'));
  assert.match(instant.slice(0, instant.indexOf('}')), /transition: none !important/);
  for (const sel of ['#controls-body.fold-instant > *', '.coordinates-panel.fold-instant',
                     '.coordinates-panel.fold-instant #coord-body'])
    assert.ok(animCss.includes(sel), `${sel} is suppressed for the read`);
});

test('the tool rows dust up past the top edge, measured before the class flips', () => {
  // Both folds ride ONE shared ritual (motion.js foldDust): measure the SHOWN box, then let the
  // caller fold, aim past the dock edge, and give the collapse the fold's slower exit clock.
  const fold = motionJs.slice(motionJs.indexOf('export function foldDust'));
  assert.ok(fold.indexOf('foldBox(el, scope, cls, false, FOLD_INSTANT_CLASS)') !== -1);
  assert.ok(fold.indexOf('foldBox(') < fold.indexOf('toggle?.();'),
    'the box is measured before the fold starts');
  assert.match(fold, /const away = box && dockAwayPoint\(box, dock\);/);
  assert.match(fold, /ms: hiding \? FOLD_DUST_OUT_MS : inMs/);
  assert.match(toolbarJs,
    /foldDust\(body, body, 'hidden', hidden, 'top',\s*\n?\s*\{ toggle: \(\) => body\.classList\.toggle\('hidden', hidden\) \}\);/);
});

test('the points table pours out past the right edge the panel collapses towards', () => {
  assert.match(mainContentJs,
    /foldDust\(body, panel, 'coord-collapsed', hidden, 'right',\s*\n?\s*\{ inMs: 460, toggle: \(\) => panel\.classList\.toggle\('coord-collapsed', hidden\) \}\);/);
  // .coord-folding takes the table out of the layout — it must be off for the read.
  assert.ok(mainContentJs.indexOf("panel.classList.remove('coord-folding')")
            < mainContentJs.indexOf('foldDust(body, panel'),
    'the fold hold is lifted before the box is read');
});

test('neither fold dusts under reduced motion — the box is not even measured', () => {
  const fold = motionJs.slice(motionJs.indexOf('export function foldDust'));
  assert.match(fold, /motionReduced\(\) \? null : foldBox\(/,
    'reduced motion skips the two forced layouts as well as the flight');
});
