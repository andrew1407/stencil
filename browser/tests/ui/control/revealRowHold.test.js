// A group sliding open inside the toolbar's wrapping rows (js/ui/motion/control/revealControls.js): the outermost
// wrapping row is held at the height it ends at for the slide, so the rows below (the canvas an arrival has just
// measured among them) never jump a row mid-slide; a closing group holds nothing, its row closes with it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDustPage, boxEl, rect } from '../../helpers/dustRig.js';
import { createStubElement } from '../../helpers/dom.js';

const { revealControls } = await import('../../../js/ui/motion/control/revealControls.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');

// controls (wrap) > section > row (wrap) > group: the toolbar is `tall` px while the group shows, else 257.
const toolbar = (tall) => {
  const group = boxEl(() => (group.style.display === 'none' ? rect(0, 0, 0, 0) : rect(40, 10, 180, 34)));
  group.style.display = 'none';
  const shows = () => group.style.display !== 'none' && group.style.maxWidth !== '0px';
  const controls = boxEl(() => rect(0, 20, 1300, shows() ? tall : 257));
  const section = createStubElement('div');
  const row = boxEl(() => rect(0, 30, 400, 34));
  controls.__wrap = row.__wrap = 'wrap';
  controls.appendChild(section);
  section.appendChild(row);
  row.appendChild(group);
  return { controls, row, group };
};

const page = (t) => installDustPage(t, { getComputedStyle: (el) => ({ flexWrap: el.__wrap || 'nowrap', getPropertyValue: () => '' }) });

test('opening, the outermost wrapping row holds the height it ends at, then lets go', (t) => {
  const p = page(t);
  setMotionPrefs({ mode: 'slide' });
  const { controls, row, group } = toolbar(322);
  assert.equal(revealControls(group, true), true);
  assert.equal(controls.style.minHeight, '322px', 'held at the wrapped height, before the group collapses to 0');
  assert.ok(!row.style.minHeight, 'only the outermost row: the toolbar the canvas sits under');
  for (const ms of p.delays()) p.fire(ms);
  assert.equal(controls.style.minHeight, '', 'released once the slide has run');
});

// Closing never holds: the row may be a bar that closes along with the group, and held it stood at full
// height until it vanished at once (user report).
test('closing leaves the row free to close with the group', (t) => {
  const p = page(t);
  setMotionPrefs({ mode: 'slide' });
  const { controls, group } = toolbar(322);
  group.style.display = 'inline-flex';
  assert.equal(revealControls(group, false), true);
  assert.equal(controls.style.minHeight ?? '', '', 'no hold on the way out');
  for (const ms of p.delays()) p.fire(ms);
  assert.equal(group.style.display, 'none');
});

test('two slides in one row release it once, after the later', (t) => {
  const p = page(t);
  setMotionPrefs({ mode: 'slide' });
  const { controls, group } = toolbar(322);
  const other = boxEl(() => (other.style.display === 'none' ? rect(0, 0, 0, 0) : rect(240, 10, 90, 34)));
  other.style.display = 'none';
  controls.children[0].children[0].appendChild(other);
  revealControls(group, true);
  const first = p.delays();
  revealControls(other, true, 'inline-flex', { ms: 900 });   // its own, longer clock
  for (const ms of first) p.fire(ms);
  assert.equal(controls.style.minHeight, '322px', 'the later slide still holds it');
  for (const ms of p.delays()) p.fire(ms);
  assert.equal(controls.style.minHeight, '');
});

// Asked back while still leaving (a search typed on): the leave lands at once and the group comes back.
// Before, the leave's end hid it for good while its rows were still listed (user report).
test('a reveal that interrupts a leave ends shown, not hidden by the late leave', (t) => {
  const p = page(t);
  setMotionPrefs({ mode: 'transition' });
  const { group } = toolbar(322);
  group.style.display = 'inline-flex';
  revealControls(group, false);
  p.frame(); p.frame();   // the leave is under way
  revealControls(group, true);
  p.frame(); p.frame();
  for (let i = 0; i < 3; i++) for (const ms of p.delays()) p.fire(ms);
  assert.equal(group.style.display, 'inline-flex', 'shown once every timer has run');
});

// Two groups revealed in one sweep: the second measured the row while the first sat at its 0px start, so the hold
// was a line short and the row grew once both stood, moving the frame under a flight (user report).
test('the hold counts every group still waiting at its start as standing at full width', (t) => {
  const p = page(t);
  setMotionPrefs({ mode: 'slide' });
  const { controls, group } = toolbar(322);
  const other = boxEl(() => (other.style.display === 'none' ? rect(0, 0, 0, 0) : rect(240, 10, 90, 34)));
  other.style.display = 'none';
  controls.children[0].children[0].appendChild(other);
  // The toolbar wraps one line more only once BOTH groups stand at their natural width.
  const standing = (g) => g.style.display !== 'none' && g.style.maxWidth !== '0px';
  controls.getBoundingClientRect = () => rect(0, 20, 1300, standing(group) && standing(other) ? 387 : 322);
  revealControls(group, true);
  assert.equal(group.style.maxWidth, '0px', 'the first waits at its start');
  revealControls(other, true);
  assert.equal(controls.style.minHeight, '387px', 'held at the height both end at, not the one the start wrapped to');
  assert.equal(group.style.maxWidth, '0px', 'and the first still starts from 0 — the measure put it back');
  for (const ms of p.delays()) p.fire(ms);
  assert.equal(controls.style.minHeight, '');
});
