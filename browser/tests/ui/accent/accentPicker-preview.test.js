// The accent list's hover preview (js/ui/picker.js): the resting row latches its hover and
// a preview holds the replays and the cursor across the palette flood. From accentPicker.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fillAccentMenu } from '../../../js/ui/accent/picker.js';
import { createStubElement, installDom } from '../../helpers/dom.js';

const makeEl = () => createStubElement('div', { contains: () => true });
const installDoc = () => installDom({ createElement: makeEl });

// A preview-wired list, as BOTH copies build it: resting on a row paints that accent, leaving puts the
// committed one back. The palette flood is a View Transition that drops real :hover.
const previewMenu = ({ closeOnPick = false } = {}) => {
  const doc = installDoc();
  const menu = makeEl();
  const shown = [];
  const picked = [];
  // The returned reset is what a dismissal calls — toolbar.js for the logo copy,
  // buildAccentPicker's own close() for the Visuals one.
  menu.__reset = fillAccentMenu(menu,
    (k) => {
      picked.push(k);
      doc.documentElement.classList.add('theme-instant');
      if (closeOnPick) menu.__reset();
    },
    { on: (k) => shown.push(k), off: () => shown.push(null) });
  return { doc, menu, shown, picked, row: (k) => menu.children.find((li) => li.dataset.key === k) };
};
const held = (doc, menu) => [menu.classList.contains('dd-preview-hold'),
                             doc.documentElement.classList.contains('dd-preview-cursor')];

test('the resting row LATCHES its hover, and a preview holds the replays + the cursor', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  assert.ok(row('pink').classList.contains('dd-hover'), 'the slide is a class, not just :hover');
  assert.deepEqual(held(doc, menu), [false, false], 'nothing is held before a preview shows');

  t.mock.timers.tick(300);                    // the rested-intent delay
  assert.deepEqual(shown, ['pink'], 'resting previews it');
  assert.deepEqual(held(doc, menu), [true, true], 'the flood cannot replay the sweep or blink the cursor');

  // The transition's own synthetic re-enter on the row already rested on changes nothing.
  doc.documentElement.classList.add('theme-instant');
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink'], 'no second preview of the same key');
  assert.deepEqual(held(doc, menu), [true, true], 'and the hold survives it');

  // The cursor hold ends WITH the flood, on its own — only its snapshot steals the row's
  // hit test. The replay freeze stays for as long as the preview shows.
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(doc, menu), [true, false]);
  assert.ok(row('pink').classList.contains('dd-hover'), 'the row is still the one being previewed');
});

test('a dismissal takes the latch and the hold with it — the reset the owner calls on close', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  const reset = menu.__reset;
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  reset();                                       // Escape / an outside press closed the list
  assert.deepEqual(shown, ['pink', null], 'the committed accent is back');
  assert.deepEqual(held(doc, menu), [false, false]);
  assert.ok(!row('pink').classList.contains('dd-hover'), 'nothing held over to the next open');
});

test('hopping to another row lifts the hold — the new row plays its own hover once', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('grass').dispatch('pointerenter');       // a real hop (the pointer moved off pink)
  assert.deepEqual(held(doc, menu), [false, false], 'the hop is a real hover — let it play');
  assert.ok(row('grass').classList.contains('dd-hover'), 'the latch moves with the pointer');
  assert.ok(!row('pink').classList.contains('dd-hover'), 'and only one row wears it');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink', 'grass']);
  assert.deepEqual(held(doc, menu), [true, true], 'the new preview holds again');
});

test('leaving the list drops the latch and the hold with the preview', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  menu.dispatch('pointerleave');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink', null], 'the committed accent is back');
  assert.deepEqual(held(doc, menu), [false, false]);
  assert.ok(!row('pink').classList.contains('dd-hover'));
});

test('a pick frees the cursor at once and holds the replays until the COMMIT flood is over', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, picked, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('pink').dispatch('click');                 // commits — and floods again
  assert.deepEqual(picked, ['pink']);
  assert.deepEqual(held(doc, menu), [true, false], 'the cursor goes with the closing list');

  t.mock.timers.tick(300);                       // the commit flood is still running
  assert.deepEqual(held(doc, menu), [true, false], 'the rows stay frozen under it');
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(doc, menu), [false, false], 'and are released once it settles');
  assert.ok(!row('pink').classList.contains('dd-hover'), 'the latch goes with it');
});

test('a pick that CLOSES the list keeps the rows frozen under the commit flood', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  // The logo menu's own shape: onPick applies the accent and closes, running the same
  // reset a dismissal does — which must not lift the hold from under the pick's own wipe.
  const { doc, menu, row } = previewMenu({ closeOnPick: true });
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('pink').dispatch('click');
  assert.deepEqual(held(doc, menu), [true, false], 'the dissolving rows cannot replay their hover');
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(doc, menu), [false, false], 'released once the commit flood settles');
});

test('a row passing under the pointer as the list closes cannot start a new preview', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, shown, row } = previewMenu({ closeOnPick: true });
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('pink').dispatch('click');
  // The list is leaving under the pointer: whatever it drags past must not paint.
  row('grass').dispatch('pointerenter');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink', null],
    'the close reverts its own preview, and the row dragged past starts no new one');
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
});
