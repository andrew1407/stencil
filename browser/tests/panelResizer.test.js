import { test } from 'node:test';
import assert from 'node:assert';
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
