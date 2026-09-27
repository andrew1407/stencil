// A real Storage (js/core/storage/storage.js) on a stub page: the picture's canvas and its overlay
// in #canvas-viewport, every 2D call the page's canvases make in one `ops` log ([canvas, method,
// ...args]), an in-memory registry and the partial app newTemporary and loadPayloadIntoApp reach.
// setTimeout is mocked for `t`, and every global is put back after it.
import { installDom, createStubElement } from './dom.js';
import { createMemoryStorage } from './memoryStorage.js';
import { Storage } from '../../js/core/storage/storage.js';

const box = (w, h) => ({ left: 0, top: 0, width: w, height: h, right: w, bottom: h, x: 0, y: 0 });

export const mountStorage = (t, { image = true, app: extra = {} } = {}) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const env = { reduced: false };
  const ops = [];
  const canvasEl = (overrides = {}) => {
    const el = createStubElement('canvas', overrides);
    const ctx = new Proxy({}, {
      get: (o, k) => (k in o ? o[k] : k === 'getImageData'
        ? (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4).fill(255) })
        : (...args) => { ops.push([el, k, ...args]); }),
    });
    el.getContext = () => ctx;
    return el;
  };
  const createElement = (tag) => (tag === 'canvas' ? canvasEl() : createStubElement(tag));
  const doc = installDom({ autoCreateById: true, createElement }, {
    window: { devicePixelRatio: 1, innerWidth: 1280, innerHeight: 800, addEventListener() {}, removeEventListener() {} },
    localStorage: createMemoryStorage(),
    matchMedia: (q) => ({ matches: env.reduced && q.includes('reduced-motion') }),
    requestAnimationFrame: () => 0,
    cancelAnimationFrame: () => {},
    getComputedStyle: () => ({ color: '', getPropertyValue: () => '' }),
  });
  t.after(doc.restore);
  const viewport = doc.register('canvas-viewport', createStubElement('div', {
    getBoundingClientRect: () => box(400, 300), clientWidth: 400, clientHeight: 300,
  }));
  const canvas = canvasEl({ getBoundingClientRect: () => box(200, 100),
    closest: (sel) => (sel === '.canvas-viewport' ? viewport : null) });
  viewport.appendChild(canvas);
  const overlay = canvasEl();
  const chatScopes = [];
  const app = Object.assign({
    canvas, ctx: canvas.getContext('2d'), lines: [],
    renderer: { layers: () => [canvas, overlay], redraw() {} },
    history: { reset() {} },
    coordTable: { update() {} },
    zoomPan: { value: null, setZoomInputValue(v) { this.value = v; }, syncViewportHeight() {} },
    chatPersistence: { projectOpened: (scope) => chatScopes.push(scope) },
    tabs: { reportActive() {}, reportIncognito() {} },
    settings: { syncFormulaUI() {} },
    input: { setHoldDrawDelay() {} },
    updateInfo() {}, updateButtons() {}, showSaveStatus() {}, updateIncognitoUI() {},
    applyUnitToUI() {}, syncDrawModeUI() {},
  }, extra);
  // A picture on screen: its image, its zoomed backing store and the scroll it left behind.
  const rearm = ({ image: picture = {}, reduced = false } = {}) => {
    env.reduced = reduced;
    Object.assign(app, { image: picture, scale: 2.5, renderedScale: 2.5 });
    Object.assign(canvas, { width: 200, height: 100 });
    Object.assign(canvas.style, { width: '400px', height: '200px' });
    Object.assign(viewport, { scrollLeft: 240, scrollTop: 180 });
  };
  rearm({ image: image ? {} : null });
  app.storage = new Storage(app);
  return {
    doc, viewport, canvas, overlay, app, storage: app.storage, ops, chatScopes, rearm,
    // The dust layers ghostOut stood over the viewport.
    stages: () => viewport.children.filter((c) => c.className === 'canvas-dust'),
  };
};
