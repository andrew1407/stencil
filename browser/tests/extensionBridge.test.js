// Drives the REAL extension bridge (js/core/extensionBridge.js) over a stub window bus and a
// stub DrawingApp, pinning the wire contract its content script depends on: one `stencil-ext-req`
// in, exactly one id-correlated `stencil-ext-res` out — including for the requests we refuse.
// Import tests stop at importExternalImage(launch, {mode}): the loader it reaches lives in a
// PRIVATE tail no stub `this` can invoke, so what's pinned is the mode being forwarded verbatim.
import { test } from 'node:test';
import assert from 'node:assert/strict';

// The thumbnail path draws the live #canvas into an offscreen one; count the elements it
// creates so "skip the capture" is observable.
let created = [];
globalThis.document = {
  createElement(tag) {
    const el = { tag, width: 0, height: 0, getContext: () => ({ drawImage() {} }), toDataURL: () => 'data:image/jpeg;base64,THUMB' };
    created.push(el);
    return el;
  },
};

const { wireExtensionBridge } = await import('../js/core/extensionBridge.js');

const PNG = 'data:image/png;base64,AAAA';

// A window that records what the page posts back and can deliver a message from any source.
const makeWindow = () => {
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
const makeApp = (over = {}) => {
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
const setUp = (over = {}) => {
  created = [];
  const win = makeWindow();
  const app = makeApp(over);
  wireExtensionBridge(app, win);
  return { win, app };
};

const request = (win, id, req, payload) =>
  win.deliver({ source: 'stencil-ext-req', id, request: req, payload });

// The reply chain is a promise, so let the microtasks flush before asserting.
const flush = () => new Promise((r) => setTimeout(r, 0));

test('state describes a loaded editor: project, image, live thumbnail and the project list', async () => {
  const { win } = setUp();
  request(win, 'ext-1', 'state', {});
  await flush();

  assert.equal(win.replies.length, 1);
  const res = win.replies[0];
  assert.equal(res.source, 'stencil-ext-res');
  assert.equal(res.id, 'ext-1');
  assert.equal(res.ok, true);
  assert.deepEqual(res.result, {
    projectId: 'p1',
    projectName: 'Floor plan',
    hasImage: true,
    imageName: 'plan',
    imageSize: { w: 1600, h: 1200 },
    incognito: false,
    thumbnail: 'data:image/jpeg;base64,THUMB',
    projects: [
      { id: 'p1', name: 'Floor plan', active: true },
      { id: 'p2', name: 'Roof', active: false },
    ],
  });
  // Downscaled to a 256px long edge (800×600 → 256×192), not a full-size copy.
  assert.deepEqual([created[0].width, created[0].height], [256, 192]);
});

test('state on a blank editor answers cleanly — no image, no size, no thumbnail', async () => {
  const { win } = setUp({ activeProjectId: null, imageBaseName: '', image: null, canvas: { width: 0, height: 0 } });
  request(win, 'ext-2', 'state', {});
  await flush();

  const res = win.replies[0];
  assert.equal(res.ok, true);
  assert.equal(res.result.projectId, '');
  assert.equal(res.result.projectName, '');
  assert.equal(res.result.hasImage, false);
  assert.equal(res.result.imageName, '');
  assert.equal(res.result.imageSize, null);
  assert.equal(res.result.thumbnail, '');          // a zero-sized canvas is a blank preview, not a throw
  assert.equal(res.result.projects.length, 2);     // the tab's other projects are still listed
  assert.ok(res.result.projects.every((p) => !p.active));
});

test('state reports an incognito session (the panel warns before importing over one)', async () => {
  const { win, app } = setUp();
  app.storage.incognito = true;
  request(win, 'ext-3', 'state', {});
  await flush();
  assert.equal(win.replies[0].result.incognito, true);
});

test('state with { thumbnail: false } skips the canvas capture (cheap poll refresh)', async () => {
  const { win } = setUp();
  request(win, 'ext-4', 'state', { thumbnail: false });
  await flush();

  assert.equal(win.replies[0].result.thumbnail, '');
  assert.equal(created.length, 0, 'no offscreen canvas was created');
});

test('a tainted / unencodable canvas degrades to a blank preview instead of failing the state', async () => {
  const { win } = setUp();
  globalThis.document.createElement = () => ({
    width: 0, height: 0, getContext: () => ({ drawImage() {} }),
    toDataURL() { throw new Error('SecurityError'); },
  });
  request(win, 'ext-5', 'state', {});
  await flush();
  // Restore the working stub for the rest of the file.
  globalThis.document.createElement = (tag) => {
    const el = { tag, width: 0, height: 0, getContext: () => ({ drawImage() {} }), toDataURL: () => 'data:image/jpeg;base64,THUMB' };
    created.push(el);
    return el;
  };

  assert.equal(win.replies[0].ok, true);
  assert.equal(win.replies[0].result.thumbnail, '');
});

test('import "new" normalizes the hand-off and imports it as its own project', async () => {
  const { win, app } = setUp();
  const crop = { x: 10, y: 20, width: 100, height: 140 };
  request(win, 'ext-6', 'import', {
    handoff: {
      dataUrl: PNG, name: 'shot.png', source: 'https://cdn.example/shot.png',
      resource: 'https://example.com/page', page: { size: 'A4' }, crop,
    },
    mode: 'new',
  });
  await flush();

  assert.equal(app.imports.length, 1);
  const [launch, opts] = app.imports[0];
  assert.equal(launch.kind, 'dataUrl');             // normalizeLaunchPayload classified it
  assert.equal(launch.dataUrl, PNG);
  assert.equal(launch.name, 'shot.png');
  assert.equal(launch.source, 'https://cdn.example/shot.png');
  assert.equal(launch.resource, 'https://example.com/page');
  assert.deepEqual(launch.crop, crop);
  assert.deepEqual(launch.page, { size: 'A4' });
  assert.deepEqual(opts, { mode: 'new' });
  assert.deepEqual(win.replies[0], {
    source: 'stencil-ext-res', id: 'ext-6', ok: true,
    result: { projectId: 'p1', projectName: 'Floor plan' },
  });
});

test('import "replace" and "replace-keep" are forwarded verbatim (the loader keeps/drops the lines)', async () => {
  for (const mode of ['replace', 'replace-keep']) {
    const { win, app } = setUp();
    request(win, `ext-${mode}`, 'import', { handoff: { dataUrl: PNG, name: 'x.png' }, mode });
    await flush();

    assert.deepEqual(app.imports[0][1], { mode });
    assert.equal(win.replies[0].ok, true);
    assert.equal(win.replies[0].id, `ext-${mode}`);
  }
});

test('a replace into a blank editor is refused — there is no image to replace', async () => {
  const { win, app } = setUp({ image: null });
  request(win, 'ext-7', 'import', { handoff: { dataUrl: PNG }, mode: 'replace' });
  await flush();

  assert.deepEqual(win.replies[0], {
    source: 'stencil-ext-res', id: 'ext-7', ok: false, error: 'the editor holds no image to replace',
  });
  assert.equal(app.imports.length, 0);
});

test('an unknown import mode and an unusable hand-off are refused, not guessed at', async () => {
  const { win, app } = setUp();
  request(win, 'ext-8', 'import', { handoff: { dataUrl: PNG }, mode: 'ask' });     // caller-side pseudo-mode
  request(win, 'ext-9', 'import', { handoff: { src: 'ftp://nope/x.png' }, mode: 'new' });
  request(win, 'ext-10', 'import', { handoff: { server: { url: 'http://s:8090', id: 'r1' } }, mode: 'new' });
  await flush();

  assert.equal(win.replies[0].error, 'unknown import mode');
  assert.equal(win.replies[1].error, 'the request carried no image');
  assert.equal(win.replies[2].error, 'the request carried no image');   // server refs launch tabs, not imports
  assert.ok(win.replies.every((r) => r.ok === false));
  assert.equal(app.imports.length, 0);
});

test('an incognito hand-off flips a BLANK editor incognito, but never one holding work', async () => {
  const blank = setUp({ activeProjectId: null, image: null });
  request(blank.win, 'ext-11', 'import', { handoff: { dataUrl: PNG, incognito: true }, mode: 'new' });
  await flush();
  assert.equal(blank.app.storage.incognito, true);
  assert.equal(blank.app.incognitoUiCalls, 1);

  const loaded = setUp();
  request(loaded.win, 'ext-12', 'import', { handoff: { dataUrl: PNG, incognito: true }, mode: 'new' });
  await flush();
  assert.equal(loaded.app.storage.incognito, false, 'a live project keeps saving');
  assert.equal(loaded.app.incognitoUiCalls, 0);
});

test('switch moves the tab to one of its own projects and reports the new identity', async () => {
  const { win, app } = setUp();
  request(win, 'ext-13', 'switch', { projectId: 'p2' });
  await flush();

  assert.deepEqual(app.switches, ['p2']);
  assert.deepEqual(win.replies[0], {
    source: 'stencil-ext-res', id: 'ext-13', ok: true,
    result: { projectId: 'p2', projectName: 'Roof', switched: true },
  });
});

test('switching to an id this tab does not hold is refused (never clears the editor)', async () => {
  const { win, app } = setUp();
  request(win, 'ext-14', 'switch', { projectId: 'p404' });
  request(win, 'ext-15', 'switch', {});
  await flush();

  assert.deepEqual(app.switches, []);
  assert.deepEqual(win.replies.map((r) => [r.ok, r.error]), [[false, 'unknown project'], [false, 'unknown project']]);
});

test('an unknown request type is answered with an error, not silence', async () => {
  const { win } = setUp();
  request(win, 'ext-16', 'delete-everything', {});
  await flush();

  assert.deepEqual(win.replies[0], {
    source: 'stencil-ext-res', id: 'ext-16', ok: false, error: 'unknown request',
  });
});

test('messages from another frame — or without our tag — are ignored entirely', async () => {
  const { win, app } = setUp();
  // Same shape, different source: an iframe or an opener impersonating the bridge.
  win.deliver({ source: 'stencil-ext-req', id: 'x', request: 'state', payload: {} }, { name: 'other-frame' });
  // Our window, but someone else's message bus traffic.
  win.deliver({ source: 'stencil-page-api', message: { type: 'stencil-page-open' } });
  win.deliver({ source: 'stencil-ext-req', request: 'state' });   // no id to correlate → not ours
  win.deliver(null);
  await flush();

  assert.deepEqual(win.replies, []);
  assert.equal(app.imports.length, 0);
});

test('replies are id-correlated, so overlapping requests never cross', async () => {
  const { win } = setUp();
  request(win, 'a', 'state', { thumbnail: false });
  request(win, 'b', 'switch', { projectId: 'p2' });
  request(win, 'c', 'state', { thumbnail: false });
  await flush();

  assert.deepEqual(win.replies.map((r) => r.id), ['a', 'b', 'c']);
  assert.equal(win.replies[0].result.projectId, 'p1');
  assert.equal(win.replies[2].result.projectId, 'p2');   // the switch in between is visible
});

test('wiring without a window is a no-op (Node / a page with no message bus)', () => {
  assert.doesNotThrow(() => wireExtensionBridge(makeApp(), null));
});

test('crop opens the editor\'s own crop dialog (toolbar button path); a blank editor refuses', async () => {
  const clicks = [];
  globalThis.document.getElementById = (id) => (id === 'crop-image' ? { click: () => clicks.push(id) } : null);
  try {
    const { win } = setUp();
    request(win, 'crop-1', 'crop', {});
    await flush();
    const res = win.replies[0];
    assert.equal(res.id, 'crop-1');
    assert.equal(res.ok, true);
    assert.deepEqual(clicks, ['crop-image']);

    const blank = setUp({ image: null });
    request(blank.win, 'crop-2', 'crop', {});
    await flush();
    assert.equal(blank.win.replies[0].ok, false, 'nothing to crop on a blank editor');
    assert.deepEqual(clicks, ['crop-image'], 'the button was not clicked again');
  } finally {
    delete globalThis.document.getElementById;
  }
});
