// The fullscreen hover panels come and go as dust past their own edge (js/ui/panels.js):
// a panel keeps its box for the whole out-flight and drops the class with the last motes; with
// the dust declined the class flips at once and the CSS slide plays as before.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom, createStubElement } from './helpers/dom.js';
import { createFsPanels, panelDust, syncFsTriggers, FS_TRIGGER_PX, POINTS_DUST_IN_MS } from '../js/ui/fullscreen/panels.js';
import { FOLD_DUST_OUT_MS, SURFACE_IN_MS } from '../js/ui/motion.js';

const GRACE_MS = 400;

const setup = (t, { plays = true } = {}) => {
  const doc = installDom({ autoCreateById: true });
  doc.getElementById('fs-selection-panel').style.display = 'none';
  t.after(() => doc.restore());
  t.mock.timers.enable({ apis: ['setTimeout'] });
  // The document's own elements: syncFsTriggers reads the panels' state back by id.
  const fsControlsPanel = doc.getElementById('fs-controls-panel');
  const fsPointsPanel = doc.getElementById('fs-points-panel');
  const calls = [];
  const dust = (panel, dock, hiding, inMs) => { calls.push({ id: panel.id, dock, hiding, inMs }); return plays; };
  const panels = createFsPanels({ fsControlsPanel, fsPointsPanel, showPoints: () => {}, dust });
  return { panels, fsControlsPanel, fsPointsPanel, calls };
};
const shown = (el) => el.classList.contains('fs-panel-visible');

test('a reveal dusts the strip in from the top edge and the list from the right, once', (t) => {
  const { panels, fsControlsPanel, fsPointsPanel, calls } = setup(t);
  panels.showControlsPanel();
  assert.ok(shown(fsControlsPanel));
  assert.deepEqual(calls, [{ id: 'fs-controls-panel', dock: 'top', hiding: false, inMs: SURFACE_IN_MS }]);
  panels.showControlsPanel();
  assert.equal(calls.length, 1, 'a strip already up does not form again');
  panels.showPointsPanel();
  assert.ok(shown(fsPointsPanel));
  assert.deepEqual(calls[1], { id: 'fs-points-panel', dock: 'right', hiding: false, inMs: 460 });
});

test('a hide waits out the grace, then the whole out-flight, before the class goes', (t) => {
  const { panels, fsControlsPanel, calls } = setup(t);
  panels.showControlsPanel();
  panels.hideControlsPanel();
  t.mock.timers.tick(GRACE_MS - 1);
  assert.equal(calls.length, 1, 'nothing leaves inside the grace');
  t.mock.timers.tick(1);
  assert.deepEqual(calls[1], { id: 'fs-controls-panel', dock: 'top', hiding: true, inMs: undefined });
  assert.ok(shown(fsControlsPanel), 'the box stays for the motes');
  t.mock.timers.tick(FOLD_DUST_OUT_MS - 1);
  assert.ok(shown(fsControlsPanel));
  t.mock.timers.tick(1);
  assert.ok(!shown(fsControlsPanel), 'the class goes with the last motes');
});

test('with the dust declined the class flips at once after the grace (the CSS slide)', (t) => {
  const { panels, fsPointsPanel } = setup(t, { plays: false });
  panels.showPointsPanel();
  panels.hidePointsPanel();
  t.mock.timers.tick(GRACE_MS);
  assert.ok(!shown(fsPointsPanel));
});

test('a cursor back on a dissolving panel brings it back', (t) => {
  const { panels, fsPointsPanel, calls } = setup(t);
  panels.showPointsPanel();
  panels.hidePointsPanel();
  t.mock.timers.tick(GRACE_MS + 50);
  panels.pausePointsHide();
  assert.equal(calls.at(-1).hiding, false, 'the list forms again out of its own motes');
  t.mock.timers.tick(FOLD_DUST_OUT_MS);
  assert.ok(shown(fsPointsPanel), 'the cancelled drop never fires');
});

test('a show inside the grace only cancels the hide', (t) => {
  const { panels, fsControlsPanel, calls } = setup(t);
  panels.showControlsPanel();
  panels.hideControlsPanel();
  t.mock.timers.tick(100);
  panels.showControlsPanel();
  t.mock.timers.tick(GRACE_MS + FOLD_DUST_OUT_MS);
  assert.equal(calls.length, 1);
  assert.ok(shown(fsControlsPanel));
});

test('reset settles both panels for the exit, so the next reveal forms again', (t) => {
  const { panels, fsControlsPanel, fsPointsPanel, calls } = setup(t);
  panels.showControlsPanel();
  panels.showPointsPanel();
  panels.reset();
  assert.ok(!shown(fsControlsPanel) && !shown(fsPointsPanel));
  panels.showControlsPanel();
  assert.equal(calls.length, 3);
  assert.equal(calls[2].hiding, false);
});

test('the cloned points panel drops its collapse chevron and relays a tab click to the original', () => {
  const clonesJs = readFileSync(new URL('../js/ui/fullscreen/clones.js', import.meta.url), 'utf8');
  assert.match(clonesJs, /clone\.querySelector\('#fs-clone-toggle-coord-panel'\)\?\.remove\(\);/,
    'the hover panel hides on its own, so the chevron goes');
  // A tab in the copy has no handler of its own: the original switches, then the copy is rebuilt.
  assert.match(clonesJs, /for \(const tab of \['coord-tab-points', 'coord-tab-lines'\]\)/);
  assert.match(clonesJs, /document\.getElementById\(tab\)\?\.click\(\);\s*populateFsPoints\(fsPointsPanel\);/);
});

test('the reveal band is 28px while a panel is away and shrinks into its padding once revealed', (t) => {
  const { panels, fsControlsPanel, fsPointsPanel } = setup(t);
  const top = document.getElementById('fs-top-trigger');
  const right = document.getElementById('fs-right-trigger');
  // The bands are the ones the stylesheet starts from, so a cold entry and a synced one agree.
  assert.equal(FS_TRIGGER_PX, 28);
  const css = readFileSync(new URL('../css/components/fullscreen.css', import.meta.url), 'utf8');
  assert.match(css, /#fs-top-trigger \{[^}]*height: 28px;/);
  assert.match(css, /#fs-right-trigger \{[^}]*width: 28px;/);
  syncFsTriggers();
  assert.equal(top.style.height, '28px');
  assert.equal(right.style.width, '28px');
  panels.showControlsPanel();
  assert.equal(top.style.height, '8px', 'a revealed strip is its own keep-zone');
  assert.equal(right.style.width, '28px');
  panels.showPointsPanel();
  assert.equal(right.style.width, '8px');
  panels.hideControlsPanel();
  panels.hidePointsPanel();
  t.mock.timers.tick(GRACE_MS);
  t.mock.timers.tick(FOLD_DUST_OUT_MS);
  assert.ok(!shown(fsControlsPanel) && !shown(fsPointsPanel));
  assert.equal(top.style.height, '28px');
  assert.equal(right.style.width, '28px');
});

test('the drag handle forms and leaves with the dusted list, on the list\'s own clock', () => {
  const css = readFileSync(new URL('../css/components/fullscreen.css', import.meta.url), 'utf8');
  assert.match(css, /#fs-points-panel\.dust-driven ~ #fs-panel-resizer \{ transition: none; \}/, 'no slide of its own');
  assert.match(css, new RegExp(`#fs-points-panel\\.surface-forming ~ #fs-panel-resizer \\{ animation: surfaceForm ${POINTS_DUST_IN_MS}ms linear both; \\}`));
  assert.match(css, /#fs-points-panel\.surface-leaving ~ #fs-panel-resizer \{ opacity: 0; pointer-events: none; \}/);
});

test('panelDust declines a panel without a box and never throws', (t) => {
  const doc = installDom({ autoCreateById: true });
  t.after(() => doc.restore());
  assert.equal(panelDust(createStubElement('div'), 'top', false), false);
  assert.equal(panelDust(createStubElement('div'), 'right', true), false);
});
