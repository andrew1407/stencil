// The mark's veil and its wiring (js/ui/swap.js, customSelect.js): one delegated
// listener per checkbox, a word exchanged only on a real change, filtered rows in as sand.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import {
  MARK_FORMING_CLASS, FILTER_DUST_MS, FILTER_DUST_DRIFT, FILTER_ENTER_MS, markIn, markOut,
  markSwap, revealControls, settleMark, filterDust,
} from '../../../js/ui/motion.js';
import { motionSource } from '../../helpers/motionSource.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const motionJs = motionSource();
const swapJs = read('../../../js/ui/control/swap.js');
const animCss = ANIMATIONS_CSS;
const selectJs = read('../../../js/ui/control/customSelect.js');

test('the veil hides the MARK, never the control around it', () => {
  assert.equal(MARK_FORMING_CLASS, 'mark-forming');
  const rule = animCss.slice(animCss.indexOf('.mark-forming {'));
  assert.match(rule, /@keyframes markForm \{ 0%, 6\d% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/,
    'the real mark waits until the motes have very nearly landed');
  // A checkbox keeps its outline throughout: only the accent fill and the tick are the
  // mark, so the veil suppresses those instead of the whole control's opacity.
  const box = rule.slice(rule.indexOf('input[type=checkbox].mark-forming'));
  const body = box.slice(0, box.indexOf('}'));
  assert.match(body, /animation: none;/, 'no opacity fade — that would blink the border');
  assert.match(body, /background-color: transparent;/);
  assert.match(body, /background-image: none;/);
  assert.match(animCss.slice(animCss.indexOf('.mark-forming {')),
    /@media \(prefers-reduced-motion: reduce\) \{\s*\n\s*\.mark-forming \{ animation: none; \}/);
});

test('one delegated listener wires every checkbox, as one filter does on the desktop', () => {
  assert.match(swapJs, /root\.addEventListener\('change'/, 'a click on the box or its label');
  assert.match(swapJs, /export function setChecked/, '…and a programmatic set animates too');
  assert.match(swapJs, /if \(el\.checked === on\) return;/,
    'a set to the state it already has is not a change to show');
  // The checked ink is READ, not guessed (the accent in the app, theme text in the context
  // menu's twin), and cached: by the time an UNcheck is seen the element paints nothing.
  assert.match(swapJs, /if \(el\.checked\) \{ el\.__markInk = read\(\); return el\.__markInk; \}/);
  assert.match(swapJs, /if \(el\.__markInk\) return el\.__markInk;/);
  assert.match(swapJs, /if \(el\.type !== 'checkbox'\) return getComputedStyle\(el\)\.borderTopColor;/,
    'a radio cannot be poked for a reading — ticking one unticks its group');
  assert.match(swapJs, /if \(!ink\) return false;/,
    'an indicator that paints nothing has nothing to scatter (the f(x,y) pill)');
});

test('a select exchanges its chosen word, but only on a real change', () => {
  assert.match(selectJs, /markSwap\(cur, \(\) => \{ cur\.textContent = label; \}\)/);
  assert.match(selectJs, /const changed = settled && shown !== null && label !== shown;\s*\n\s*if \(!changed\) cur\.textContent = label;/,
    'the first paint and a re-sync on open write straight through');
  // …and so does BOOT: applyUnitToUI writes the restored page size and unit in the same
  // task that wired the toolbar, and both dropdowns dealt themselves in as the page opened.
  assert.match(selectJs, /requestAnimationFrame\(\(\) => \{ settled = true; \}\)/);
  assert.match(selectJs, /else settled = true;/, 'off-browser, every set is a real one');
});

test('a filter brings its rows in as sand — and never plays one out', () => {
  assert.ok(FILTER_DUST_MS <= FILTER_ENTER_MS * 2,
    'a view change has to keep up with typing in a search box');
  assert.ok(FILTER_DUST_DRIFT < 1, 'and it must never read as the delete it is not');
  const body = motionJs.slice(motionJs.indexOf('export const filterDust ='),
                              motionJs.indexOf('export const filterDelta'));
  assert.match(body, /scatterGridFor\(count, index\)/,
    'one shared mesh budget across every row the change moves');
  assert.match(body, /if \(!cols/, 'past the row ceiling a row simply fades, as it always did');
  assert.match(body, /paintTile: speckPainter\(el\)/, 'anonymous specks, never copies of a row');
  // One direction only: what a filter drops was never destroyed, so it does not come
  // apart — it is simply not the answer any more, and the answer is what arrives.
  assert.match(body, /gather: true/);
  assert.ok(!/gather: false|hostClass: 'dust-leaving'/.test(body), 'there is no exit to play');
  assert.match(motionJs, /filterDust\(el, i, rows\.length, boxes\[i\]\);/);
});

test('nothing here throws off-browser, or on a stub — decoration is never load-bearing', () => {
  for (const fn of [markIn, markOut, settleMark])
    assert.doesNotThrow(() => fn(null));
  assert.equal(markIn(null), false);
  assert.equal(markOut(undefined), false);
  assert.equal(markSwap({}, null), false, 'no writer, no swap');
  assert.equal(revealControls(null, true), false);
  assert.equal(filterDust(null, true), false);
  // A swap on a stub still WRITES the value — the DOM is never behind the state.
  let wrote = 0;
  markSwap({ getBoundingClientRect: () => ({ width: 0, height: 0 }) }, () => { wrote++; });
  assert.equal(wrote, 1);
});
