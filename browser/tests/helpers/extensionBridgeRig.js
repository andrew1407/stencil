// Shared rig for the extensionBridge specs: the offscreen-canvas document stub, a window that
// records what the page posts back, and the slice of DrawingApp the bridge reads.

// The thumbnail path draws the live #canvas into an offscreen one; count the elements it
// creates so "skip the capture" is observable.
export let created = [];
globalThis.document = {
  createElement(tag) {
    const el = { tag, width: 0, height: 0, getContext: () => ({ drawImage() {} }), toDataURL: () => 'data:image/jpeg;base64,THUMB' };
    created.push(el);
    return el;
  },
};

export const { wireExtensionBridge } = await import('../../js/core/extensionBridge.js');

export const PNG = 'data:image/png;base64,AAAA';

// A window that records what the page posts back and can deliver a message from any source.
export const makeWindow = () => {
  const win = {
    replies: [],
    addEventListener(type, fn) { if (type === 'message') win.listeners.push(fn); },
    postMessage(data) { win.replies.push(data); },
  };
  win.listeners = [];
  // `source` defaults to the window itself — a same-window post, the only kind we answer.
  win.deliver = (data, source = win) => { for (const fn of win.listeners) fn({ data, source }); };
  return win;
};

// The slice of DrawingApp the bridge reads: active project + image + store, plus spies on the
// three methods it may call. `over` swaps in a blank editor, an incognito session, …
export const makeApp = (over = {}) => {
  const projects = over.projects || [{ id: 'p1', name: 'Floor plan' }, { id: 'p2', name: 'Roof' }];
  const app = {
    imports: [],
    switches: [],
    incognitoUiCalls: 0,
    activeProjectId: 'p1',
    imageBaseName: 'plan',
    image: { naturalWidth: 1600, naturalHeight: 1200 },
    canvas: { width: 800, height: 600 },
    storage: {
      incognito: false,
      store: {
        list: () => projects,
        getMeta: (id) => projects.find((p) => p.id === id) || null,
      },
    },
    updateIncognitoUI() { app.incognitoUiCalls++; },
    switchToProject(id) { app.switches.push(id); app.activeProjectId = id; return true; },
    importExternalImage(launch, opts) { app.imports.push([launch, opts]); return Promise.resolve(); },
    ...over,
  };
  return app;
};

// Wire a fresh bridge and return { win, app } plus a request sender.
export const setUp = (over = {}) => {
  created = [];
  const win = makeWindow();
  const app = makeApp(over);
  wireExtensionBridge(app, win);
  return { win, app };
};

export const request = (win, id, req, payload) =>
  win.deliver({ source: 'stencil-ext-req', id, request: req, payload });

// The reply chain is a promise, so let the microtasks flush before asserting.
export const flush = () => new Promise((r) => setTimeout(r, 0));
