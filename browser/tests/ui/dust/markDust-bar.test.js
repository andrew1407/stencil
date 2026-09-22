// revealBar (js/ui/motion.js): a bar opens at once but defers its close by its contents'
// flight, holding then zeroing the whole footprint; plus the modal dust sweep.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import {
  REVEAL_GROUP_OUT_MS, revealControls, revealBar, BAR_HELD_CLASS, BAR_CLOSING_CLASS,
} from '../../../js/ui/motion.js';
import { motionSource } from '../../helpers/motionSource.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const motionJs = motionSource();
const swapJs = read('../../../js/ui/control/swap.js');
const animCss = ANIMATIONS_CSS;
const selectJs = read('../../../js/ui/control/customSelect.js');

// The bar's slot CLIPS its controls (overflow:hidden under a collapsing max-height), so its
// hide is deferred and its open immediate, as ConnectDialog::updateBatchBar does (user report).
test('revealBar defers the CLOSE by its contents\' flight, but opens at once', () => {
  const jobs = [];
  const setTimer = (fn, ms) => { jobs.push({ fn, ms }); return jobs.length; };
  // A stub, not a DOM node: revealControls measures the slot it is collapsing.
  const bar = { style: { display: 'flex' }, getBoundingClientRect: () => ({ width: 0, height: 0 }) };

  // Closing: nothing happens this turn…
  assert.equal(revealBar(bar, () => false, { setTimer }), false);
  assert.equal(bar.style.display, 'flex', 'the strip is still there while the buttons fly');
  assert.equal(jobs.length, 1);
  assert.equal(jobs[0].ms, REVEAL_GROUP_OUT_MS, 'the group out-flight is the default wait');
  jobs[0].fn();
  assert.equal(bar.style.display, 'none', '…and only then does the slot go');

  // …and a removal's own clock is honoured, so the strip lands with its rows.
  bar.style.display = 'flex';
  jobs.length = 0;
  revealBar(bar, () => false, { ms: 220, setTimer });
  assert.equal(jobs[0].ms, 220);

  // Re-asked on arrival: a selection made while the flight played keeps the bar.
  bar.style.display = 'flex';
  jobs.length = 0;
  let wanted = false;
  revealBar(bar, () => wanted, { setTimer });
  wanted = true;
  jobs[0].fn();
  assert.equal(bar.style.display, 'flex', 'want() said yes in the meantime');

  // Opening never waits — the controls need the slot before they can fly into it.
  bar.style.display = 'none';
  jobs.length = 0;
  revealBar(bar, () => true, { setTimer });
  assert.equal(jobs.length, 0, 'no timer at all on the way in');
  assert.equal(bar.style.display, 'flex');
  // …and an already-closed bar asked to close again settles now, not a flight later.
  jobs.length = 0;
  bar.style.display = 'none';
  revealBar(bar, () => false, { setTimer });
  assert.equal(jobs.length, 0);
  // Decoration is never load-bearing.
  assert.doesNotThrow(() => revealBar(null, () => false, { setTimer }));
  // The bar's own box is never animated either way — a sliding max-height clips Select all on
  // the way in and leaves a bare grey line on the way out (user report); only the controls fly.
  assert.equal(bar.style.maxHeight, undefined);
  const src = motionSource();
  const body = src.slice(src.indexOf('export const revealBar'), src.indexOf('const REVEAL_GROUP_TRANSITION_CLASS'));
  assert.ok(!/revealControls|slideRevealSize|maxHeight/.test(body), 'a display flip, nothing more');
});

// The strip's footprint is HELD from the moment it is asked to leave, then height, padding and
// divider close together (user report); desktop twin: controlReveal holdBarSlot/closeBarSlot.
test('a leaving bar HOLDS its slot, then closes the whole footprint', () => {
  const jobs = [];
  const setTimer = (fn, ms) => { jobs.push({ fn, ms }); return jobs.length; };
  const classes = new Set();
  const props = new Map();
  const bar = {
    style: {
      display: 'flex',
      setProperty: (k, v) => props.set(k, v),
      removeProperty: (k) => props.delete(k),
      getPropertyValue: (k) => props.get(k) || '',
    },
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
    },
    getBoundingClientRect: () => ({ width: 300, height: 47 }),
  };

  revealBar(bar, () => false, { setTimer });
  assert.ok(classes.has(BAR_HELD_CLASS), 'held the instant it is asked to leave');
  assert.equal(props.get('--bar-h'), '47px', '…at the footprint it actually has');
  assert.ok(!classes.has(BAR_CLOSING_CLASS), 'but nothing closes while the controls fly');
  assert.equal(bar.style.display, 'flex');

  jobs[0].fn();
  assert.ok(classes.has(BAR_CLOSING_CLASS), 'and then the slot closes');
  assert.equal(bar.style.display, 'flex', 'still in the flow while it does — that IS the slide');
  jobs[1].fn();
  assert.equal(bar.style.display, 'none', 'out of the flow only once it has closed');
  assert.equal(classes.size, 0, 'and nothing of the flight is left on it');
  assert.equal(props.size, 0);

  // Asked back mid-close: the hold comes straight off, or the bar stays squeezed.
  bar.style.display = 'flex';
  jobs.length = 0;
  revealBar(bar, () => false, { setTimer });
  assert.ok(classes.has(BAR_HELD_CLASS));
  revealBar(bar, () => true, { setTimer });
  assert.equal(classes.size, 0, 'the freeze is released when it is wanted again');
  assert.equal(bar.style.display, 'flex');
});

// The CSS half of that close: whatever the bar's footprint is made of has to go with the
// height, or the list below still drops by the leftovers in the frame it is hidden.
test('the closing bar zeroes its padding and divider, not just its height', () => {
  const css = ANIMATIONS_CSS;
  const rule = css.match(/\.bar-held\.bar-closing \{[^}]*\}/)?.[0] || '';
  for (const prop of ['height: 0', 'max-height: 0', 'padding-top: 0', 'padding-bottom: 0',
                      'border-bottom-width: 0']) {
    assert.ok(rule.includes(prop), `the close takes ${prop} with it`);
    assert.match(rule, new RegExp(`transition:[^;]*${prop.split(':')[0]}`, 's'),
      `${prop.split(':')[0]} is animated, not dropped`);
  }
  assert.match(css, /\.bar-held \{[^}]*overflow: hidden/, 'the held slot clips its contents');
});

// A cloud is parented to <body> to clear the scroller, so it outlives its window: each window
// sweeps its own on the way out, before its own close flight (user report).
test('sweepDust takes down the clouds a window started, and only those', async () => {
  const { sweepDust } = await import('../../../js/ui/motion.js');
  const made = [];
  const host = (scope) => {
    const stopped = { stopped: false };
    const el = {
      className: 'disintegrate-host',
      dataset: scope ? { dustScope: scope } : {},
      __stop: () => { stopped.stopped = true; },
      remove: () => { el.removed = true; },
      removed: false,
      stopped,
    };
    made.push(el);
    return el;
  };
  const mine = host('projects-modal-overlay');
  const alsoMine = host('projects-modal-overlay');
  const anothers = host('connect-modal-overlay');
  const loose = host(null);
  const realDoc = globalThis.document;
  globalThis.document = { querySelectorAll: () => made };
  try {
    assert.strictEqual(sweepDust('projects-modal-overlay'), 2, 'both of that window\'s clouds');
    assert.ok(mine.removed && mine.stopped.stopped, 'the layer goes, and its loop stops first');
    assert.ok(alsoMine.removed);
    assert.ok(!anothers.removed, 'another window\'s cloud is left alone');
    assert.ok(!loose.removed, '…and so is one that belongs to no window');
    assert.strictEqual(sweepDust(null), 0, 'no scope, nothing swept');
    assert.strictEqual(sweepDust({ id: 'connect-modal-overlay' }), 1, 'an element works too');
  } finally {
    globalThis.document = realDoc;
  }
});

// …and the shell is what calls it: every window closes the same way (shell.js close()).
test('every modal shell sweeps its own dust as it closes', () => {
  const shell = read('../../../js/ui/modal/shell.js');
  assert.match(shell, /const close = \(backTo = null\) => \{[\s\S]{0,400}sweepDust\(overlay\);/,
    'the shared close sweeps, so no window has to remember to');
  assert.match(shell, /import \{[^}]*sweepDust[^}]*\} from '[^']*motion\.js'/);
});
