// The "Selected Line:" bar's entrance/exit (showSelectionPanel/hideSelectionPanels): forms
// from, and disperses back into, sand like other surfaces (js/ui/motion.js surfaceIn/
// surfaceOut). Desktop mirrors it via DisintegrateOverlay over SelectedLineBar (MainWindow.cpp).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDustDom, rect as box, boxEl, cloudKind } from '../../helpers/dustRig.js';
import { LAYOUT_CSS } from '../../helpers/css.js';

const dust = installDustDom({ docOpts: { autoCreateById: true } });
const { barDustPoint, showSelectionPanel, hideSelectionPanels } = await import('../../../js/ui/panel/selectionPanel.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
const { SURFACE_DRIVEN_CLASS, SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS } = await import('../../../js/ui/motion.js');

// The real bar on the stub page: its box, #image-info, and the fill group + separator, each
// laid out only while displayed.
const shown = (w, h) => { const el = boxEl(() => (el.style.display === 'none' ? box(0, 0, 0, 0) : box(0, 0, w, h))); el.style.display = 'none'; return el; };
const liveBar = () => {
  dust.reset();
  const { doc } = dust;
  doc.register('image-info', boxEl(box(100, 300, 400, 20)));
  const panel = doc.register('selection-panel', shown(600, 44));
  const fillGroup = doc.register('sel-fill-group', shown(220, 30));
  const fillSep = doc.register('sel-fill-sep', shown(1, 24));
  const app = { pointSize: 4, defaultFillColor: '#00000000', syncFsSelectionPanel() {}, renderLinesList() {} };
  return { panel, fillGroup, fillSep, show: (line = {}) => showSelectionPanel(app, { color: '#ff0000', thickness: 2, ...line }) };
};

// A rect stub good enough for barDustPoint: only left/width/top/height/bottom are read.
const rect = ({ left = 0, width = 0, top = 0, height = 0 }) =>
  ({ left, width, top, height, bottom: top + height });
// Shared fixtures: the bar's own box, and a #image-info stub (or none) for one test.
const testBar = () => ({ getBoundingClientRect: () => rect({ left: 0, width: 600, top: 200, height: 60 }) });
const stubImageInfo = (t, r) => {
  const prior = globalThis.document;
  globalThis.document = { getElementById: (id) => (r && id === 'image-info' ? { getBoundingClientRect: () => r } : null) };
  t.after(() => { globalThis.document = prior; });
};

test('the bar flies on the shared surface-dust primitives', () => {
  setMotionPrefs({ mode: 'particles' });
  const { panel, show } = liveBar();
  show();
  assert.ok(panel.classes.has(SURFACE_DRIVEN_CLASS) && panel.classes.has(SURFACE_FORMING_CLASS), 'surfaceIn');
  assert.equal(cloudKind(panel), 'dust-forming dust-below-chat');
  hideSelectionPanels();
  assert.ok(panel.classes.has(SURFACE_LEAVING_CLASS), 'surfaceOut');
  setMotionPrefs({ mode: 'none' });
  show();
  assert.ok(!panel.classes.has(SURFACE_FORMING_CLASS) && !panel.__dustHost, 'declined: settleSurface leaves it still');
  setMotionPrefs({ mode: 'particles' });
});

// The fill group SLIDES open and closed and dusts as it goes (motion.js revealControls), its own separator
// travelling with it, so unchaining a line never makes the bar jump.
test('the fill group and its separator come and go through revealControls', () => {
  const { fillGroup, fillSep, show } = liveBar();
  show({ locked: true });
  assert.equal(fillGroup.style.display, 'flex');
  assert.equal(fillSep.style.display, 'block');
  for (const [el, prop] of [[fillGroup, 'maxWidth'], [fillSep, 'maxHeight']]) {
    assert.ok(el.classes.has('reveal-group-transition'), 'a slide, not an outright show');
    assert.equal(el.style[prop], '0px');
  }
  assert.equal(cloudKind(fillGroup), 'dust-forming', 'and the group dusts in (a 1px rule is too thin to)');
  dust.frame();
  dust.frame();
  assert.deepEqual([fillGroup.style.maxWidth, fillSep.style.maxHeight], ['220px', '24px'], 'both slots open');
  show({ locked: false });
  assert.deepEqual([fillGroup.style.display, fillSep.style.display], ['flex', 'block'], 'held until the slots close');
  assert.equal(cloudKind(fillGroup), 'dust-falling');
  assert.deepEqual([fillGroup.style.maxWidth, fillSep.style.maxWidth], ['0px', '0px'], 'and close together');
});

test('opening: the point is #image-info\'s own current rect — the reflow already ran', (t) => {
  // showSelectionPanel flips display:block (and so triggers the reflow) BEFORE ever
  // calling barDustPoint, so #image-info's rect it reads is already the post-open one.
  stubImageInfo(t, rect({ left: 100, width: 400, top: 300, height: 20 }));
  assert.deepEqual(barDustPoint(testBar()), { x: 100 + 400 / 2, y: 300 + 20 });
});

test('closing: the point is PREDICTED at image-info\'s post-close position, not its stale current one', (t) => {
  // hideSelectionPanels calls barDustPoint(el, true) before setting display:none, so
  // #image-info's rect here is still the bar's own height too low.
  stubImageInfo(t, rect({ left: 100, width: 400, top: 300, height: 20 }));
  // Predicted bottom = the bar's own top (200) + image-info's own height (20) = 220 —
  // NOT image-info's current (stale) bottom of 320.
  assert.deepEqual(barDustPoint(testBar(), true), { x: 100 + 400 / 2, y: 200 + 20 });
});

test('with no #image-info to anchor to, both directions fall back to the bar\'s own geometry', (t) => {
  stubImageInfo(t, null);
  const expected = { x: 300, y: 230 + 60 * 1.7 };   // dockAwayPoint(rect, 'bottom'): centre + reach
  assert.deepEqual(barDustPoint(testBar()), expected);
  assert.deepEqual(barDustPoint(testBar(), true), expected);
});

test('showSelectionPanel only gathers on the hidden -> visible edge', () => {
  const { panel, show } = liveBar();
  show();
  assert.equal(cloudKind(panel), 'dust-forming dust-below-chat', 'judged before display flips to block');
  assert.equal(panel.style.display, 'block');
  const first = panel.__dustHost;
  // Re-populating an already-open bar (switching which line is selected) must NOT replay it.
  show({ color: '#00ff00' });
  assert.equal(panel.__dustHost, first);
  assert.equal(dust.clouds().filter((h) => h.className.includes('below-chat')).length, 1);
});

test('hideSelectionPanels only scatters a bar that was actually visible, and hides it either way', () => {
  const { panel, show } = liveBar();
  show();
  hideSelectionPanels();
  // The CLOSING point: predicted from the bar's own top, not #image-info's stale bottom.
  assert.equal(cloudKind(panel), 'dust-leaving dust-below-chat');
  assert.equal(panel.style.display, 'none', 'the real hide happens regardless of the sand');
  const leaving = panel.__dustHost;
  hideSelectionPanels();
  assert.equal(panel.__dustHost, null, 'a hidden bar is only settled: its flight is dropped');
  assert.equal(leaving.parentNode, null);
  assert.equal(panel.style.display, 'none');
});

test('the old plain fadeIn keyframe stays as the reduced-motion / dust-declined fallback', () => {
  // settleSurface() never adds SURFACE_DRIVEN_CLASS, so without this rule a bar whose dust declined would
  // snap in with no motion at all.
  const css = LAYOUT_CSS;
  assert.match(css, /#selection-panel \{[\s\S]*?animation: fadeIn 0\.15s ease;[\s\S]*?\}/);
});

// 'transparent' in a colour field reads as #000000 and its box as 255, so the pair would claim an
// opaque black fill: an unfilled area shows the default colour at 0 instead, as the desktop's does.
test('an unfilled area\'s fill pair shows the default colour at alpha 0', () => {
  liveBar();
  const app = { pointSize: 4, defaultFillColor: '#336699', syncFsSelectionPanel() {}, renderLinesList() {} };
  const pair = () => [dust.doc.getElementById('sel-fill').value, dust.doc.getElementById('sel-fill-alpha').value];
  showSelectionPanel(app, { color: '#ff0000', thickness: 2, locked: true, fillColor: 'transparent' });
  assert.deepEqual(pair(), ['#336699', '0']);
  showSelectionPanel(app, { color: '#ff0000', thickness: 2, locked: true, fillColor: '#ff000080' });
  assert.deepEqual(pair(), ['#ff0000', '128'], 'a filled area shows its own fill');
});
