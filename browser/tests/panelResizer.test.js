import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { installDom, createStubElement } from './helpers/dom.js';

// The coordinates-panel resizer's RESTORE path (js/utils.js wirePanelResizer): a saved
// width is re-clamped against the live window on every resize; a panel the user never
// dragged is left on its CSS default — never pinned to whatever it measures at the time.

const setup = ({ saved = null, winW = 1400, panelW = 405 } = {}) => {
  const style = { props: new Map(), setProperty(k, v) { this.props.set(k, v); }, removeProperty(k) { this.props.delete(k); }, getPropertyValue(k) { return this.props.get(k) || ''; } };
  const doc = installDom({}, {
    window: { innerWidth: winW, listeners: {}, addEventListener(t, fn) { (this.listeners[t] ||= []).push(fn); }, removeEventListener() {} },
    sessionStorage: { getItem: (k) => (k === 'drawingApp_coordPanelWidth' ? saved : null), setItem() {} },
  });
  doc.documentElement.style = style;
  const resizer = createStubElement('div');
  const panel = createStubElement('aside', { getBoundingClientRect: () => ({ width: panelW, height: 300, left: 0, top: 0, right: panelW, bottom: 300 }) });
  return { doc, style, resizer, panel, resize: () => (globalThis.window.listeners.resize || []).forEach((fn) => fn()) };
};

test('an undragged panel keeps the CSS default on load and on resize (no width is pinned)', async () => {
  const { wirePanelResizer } = await import('../js/utils.js');
  // Stacked under the canvas in a narrow window, the panel measures the full width —
  // the value that used to be fed back as a "preference" and ratcheted it up.
  const t = setup({ winW: 700, panelW: 648 });
  try {
    wirePanelResizer(t.resizer, t.panel, { maxFactor: 0.7, restore: true });
    assert.strictEqual(t.style.getPropertyValue('--coord-panel-width'), '', 'nothing pinned on load');
    globalThis.window.innerWidth = 2000;
    t.resize();
    assert.strictEqual(t.style.getPropertyValue('--coord-panel-width'), '', 'nothing pinned after a resize either');
  } finally { t.doc.restore(); }
});

test('a saved width is restored and re-clamped against the live window on every resize', async () => {
  const { wirePanelResizer, clampPanelWidth } = await import('../js/utils.js');
  const t = setup({ saved: '600', winW: 1400 });
  try {
    wirePanelResizer(t.resizer, t.panel, { maxFactor: 0.7, restore: true });
    assert.strictEqual(t.style.getPropertyValue('--coord-panel-width'), '600px');
    globalThis.window.innerWidth = 700;
    t.resize();
    assert.strictEqual(t.style.getPropertyValue('--coord-panel-width'), `${clampPanelWidth(600, 700)}px`, 'squeezed for the narrow window');
    globalThis.window.innerWidth = 1400;
    t.resize();
    assert.strictEqual(t.style.getPropertyValue('--coord-panel-width'), '600px', 'the preference comes back with the room');
  } finally { t.doc.restore(); }
});

// Both drag handles must kill .coordinates-panel's width transition (animations/collapse.css):
// an easing curve between the pointer and the panel edge reads as lag, not polish.
test('the suppression rule names both handles, by hooks that exist', () => {
  const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
  const rule = read('../css/animations/collapse.css')
    .match(/^([^{}]*\.coordinates-panel[^{}]*)\{\s*transition:\s*none;\s*\}/m);
  assert.ok(rule, 'the mid-drag width-transition suppression rule is gone');
  const covers = (hook) => rule[1].split(',').some((s) => s.includes(`${hook}.dragging`));
  assert.ok(covers('.panel-resizer'), `in-flow handle uncovered: ${rule[1]}`);
  assert.ok(covers('#fs-panel-resizer'), `fullscreen handle uncovered: ${rule[1]}`);
  const markup = read('../js/ui/panel/mainContent.js') + read('../js/ui/fullscreen/fullscreenMarkup.js');
  assert.ok(markup.includes('class="panel-resizer"'), 'no element carries .panel-resizer');
  assert.ok(markup.includes('id="fs-panel-resizer"'), 'no element carries #fs-panel-resizer');
  assert.ok(read('../js/utils/panelResizer.js').includes("classList.add('dragging')"), 'the drag no longer flags its handle');
});
