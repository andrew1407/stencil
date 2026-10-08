// A .stc handed over with the image: the VS Code extension puts the script in the SAME
// `#stencil=` fragment, as a top-level `script` key the shared normalizer ignores, so no other
// surface's codec moved. applyExternalLaunch only stashes it — index.js opens it in the Script window once the
// promise it returns has settled, because the layer order forbids core/ reaching console/.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';
import { installFetchStub } from '../../helpers/fetchStub.js';

const notifications = [];
installDom({}, {
  location: { hash: '', pathname: '/app', search: '' },
  history: { replaceState: () => {} },
}).register('notify-balloon', { notify: (msg, type) => notifications.push([msg, type]) });

let fetchImpl = () => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) });
const fetchStub = installFetchStub((...args) => fetchImpl(...args));
const fetchCalls = fetchStub.calls;

const { applyExternalLaunch, MAX_LAUNCH_SCRIPT } = await import('../../../js/core/launch/controller.js');
const constants = (await import('../../../../common/config/constants.json', { with: { type: 'json' } })).default;

// The decode boundary: the real load reads its file and the picture decodes after `decode.delayMs`,
// or never; the settle's crop rebuild is where the mock's picture appears. `reads` is every load.
const reads = [];
const decode = { delayMs: 0, never: false };
globalThis.FileReader = class {
  readAsDataURL(file) {
    reads.push(file);
    queueMicrotask(() => this.onload?.({ target: { result: 'data:image/png;base64,AAAA' } }));
  }
};
globalThis.Image = class {
  width = 4; height = 3;
  set src(v) { this.url = v; if (!decode.never) setTimeout(() => this.onload?.(), decode.delayMs); }
  get src() { return this.url; }
};

const resetGlobals = () => {
  notifications.length = 0;
  reads.length = 0;
  Object.assign(decode, { delayMs: 0, never: false });
  fetchStub.reset();
  globalThis.location = { hash: '', pathname: '/app', search: '' };
  globalThis.history = { replaceState: () => {} };
};

// `image` is the decoded picture: the load path sets it asynchronously, and a handed-over
// script must wait for it rather than run against the page behind it.
const makeMock = (over = {}) => {
  const mock = {
    image: null,
    storage: { incognito: false, temporary: false, store: {}, save() {} },
    activeProjectId: 'p1',
    lines: [],
    canvas: { width: 4, height: 3 },
    imageModel: {
      roundRect: (r) => ({ ...r }),
      defaultCropRect: () => ({ x: 0, y: 0, width: 4, height: 3 }),
      rebuildCroppedImage: () => { mock.image = { decoded: true }; },
    },
    history: { reset() {} },
    zoomPan: { fitToWindow() {} },
    coordTable: { update() {} }, hideSelectionPanels() {},
    renderer: { redraw() {}, layers: () => [] },
    tabs: { reportActive() {}, reportIncognito() {} },
    updateInfo() {}, updateButtons() {}, updateCoordStatus() {}, updateIncognitoUI() {},
    ...over,
  };
  return mock;
};

const fragmentFor = (payload) => '#stencil=' + encodeURIComponent(JSON.stringify(payload));
const run = (mock) => applyExternalLaunch(mock);

test('a script rides the fragment onto pendingLaunchScript, and the import promise gates it', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ src: 'https://cdn.example/i.png', script: '@crop 10%\n' });
  const mock = makeMock();
  const done = run(mock);

  assert.equal(mock.pendingLaunchScript, '@crop 10%\n');
  assert.equal(reads.length, 0, 'nothing has loaded yet — the caller must await');
  await done;
  assert.equal(reads.length, 1, 'the promise resolves only once the picture is in');
  assert.deepEqual(mock.image, { decoded: true });
});

test('a script-only hand-off survives the no-image path (nothing to normalize, still runnable)', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ script: '@filter sepia\n' });
  const mock = makeMock();
  await run(mock);

  assert.equal(mock.pendingLaunchScript, '@filter sepia\n');
  assert.equal(fetchCalls.length, 0, 'no image was named, so nothing is fetched');
  assert.equal(notifications.length, 0, 'and a picture-less hand-off is not an error');
});

test('a script-only hand-off can still ask for incognito', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ script: '@filter sepia\n', incognito: true });
  const mock = makeMock();
  await run(mock);

  assert.equal(mock.storage.incognito, true, 'the flag rides a payload that normalizes to nothing');
  assert.equal(mock.pendingLaunchScript, '@filter sepia\n');
});

test('a non-string, empty or oversize script is ignored', async () => {
  for (const script of [42, null, { text: '@crop 10%' }, ['@crop 10%'], '', 'x'.repeat(MAX_LAUNCH_SCRIPT + 1)]) {
    resetGlobals();
    globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', script });
    const mock = makeMock();
    await run(mock);
    assert.equal(mock.pendingLaunchScript, '', `${JSON.stringify(script)?.slice(0, 20)} is not a script`);
  }
  // …and the one exactly at the cap is.
  resetGlobals();
  const atCap = '#'.repeat(MAX_LAUNCH_SCRIPT);
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', script: atCap });
  const mock = makeMock();
  await run(mock);
  assert.equal(mock.pendingLaunchScript.length, MAX_LAUNCH_SCRIPT);
});

test('every applyExternalLaunch exit answers with a promise, so the caller can always chain', async () => {
  // No fragment, a malformed one, an oversize hash, and junk that normalizes to nothing.
  const hashes = [
    '#something-else',
    '#stencil=' + encodeURIComponent('{"dataUrl":"data:image/png;base64,AAA'),
    '#stencil=' + 'x'.repeat(33 * 1024 * 1024),
    fragmentFor({ nothing: true }),
  ];
  for (const hash of hashes) {
    resetGlobals();
    globalThis.location.hash = hash;
    const returned = run(makeMock());
    assert.ok(returned instanceof Promise, `${hash.slice(0, 24)}… still returns a promise`);
    await returned;
  }
});

// The decode is asynchronous and carries no promise of its own: a script sent WITH a picture
// must not run against the blank page behind it.
test('the promise waits for the picture to decode, not just for the load to start', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', script: '@crop 10%\n' });
  decode.delayMs = 40;
  const mock = makeMock();
  await run(mock);
  assert.equal(reads.length, 1, 'the load started');
  assert.deepEqual(mock.image, { decoded: true }, 'the caller is handed a picture, not a pending one');
});

test('a picture that never decodes gives up rather than hanging the boot', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', script: '@crop 10%\n' });
  // The load starts and nothing ever arrives. The 8s deadline is real, so run the clock fast
  // instead of waiting it out — every reading is a second later than the last.
  const realNow = Date.now;
  let clock = realNow();
  Date.now = () => { clock += 1000; return clock; };
  decode.never = true;
  const mock = makeMock();
  try {
    await run(mock);
  } finally {
    Date.now = realNow;
  }
  assert.equal(reads.length, 1, 'the load started');
  assert.equal(mock.image, null, 'no picture — and the boot carried on anyway');
});

test('the script cap is the shared LAUNCH.scriptMaxChars', () => {
  assert.equal(MAX_LAUNCH_SCRIPT, constants.LAUNCH.scriptMaxChars);
});

test('scriptMode only rides the wire: open and run both just hand the script over', async () => {
  for (const scriptMode of ['open', 'run', undefined]) {
    resetGlobals();
    globalThis.location.hash = fragmentFor({ script: '@filter sepia\n', scriptMode });
    const mock = makeMock();
    await run(mock);
    assert.equal(mock.pendingLaunchScript, '@filter sepia\n', String(scriptMode));
    assert.equal('pendingLaunchScriptMode' in mock, false, 'nothing decides to run it');
  }
});
