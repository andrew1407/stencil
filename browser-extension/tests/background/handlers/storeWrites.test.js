// The worker end of the store-write relay: an extension page's pin and ledger writes land on the
// worker's own chain, so two documents writing at once lose nothing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { pinWrites, setPinned, loadPins } from '../../../src/lib/prefs/pins.js';
import { recordOpened, loadLedger } from '../../../src/lib/prefs/ledger.js';
import { routeStoreWrites } from '../../../src/lib/prefs/writeChain.js';
import { storeWriteHandlers } from '../../../src/background/handlers/storeWrites.js';
import { MSG } from '../../../src/lib/messages.js';

const EXT = 'chrome-extension://abc/';
const PANEL = { url: `${EXT}src/popup/popup.html` };

const install = () => {
  const stub = installChromeStub();
  globalThis.chrome.runtime.getURL = (p) => EXT + p;
  return stub;
};
const handle = (msg, sender = PANEL) =>
  new Promise((resolve) => storeWriteHandlers[MSG.STORE_WRITE](msg, sender, resolve));
const pin = (n) => ({ source: `https://cdn/${n}.png`, site: 'https://a.example', pinned: true });

test('two documents with their own chains lose a pin; the stub models that race', async () => {
  install();
  const other = await import('../../../src/lib/prefs/pins.js?otherDocument');
  await Promise.all([pinWrites.setPinned(pin(1)), other.pinWrites.setPinned(pin(2))]);
  assert.equal((await loadPins()).length, 1);
});

test('a panel write routed to the worker shares the worker chain with its own writes', async () => {
  install();
  routeStoreWrites((m) => handle(m));
  try {
    await Promise.all([
      pinWrites.setPinned(pin(1)),                    // the worker's own (context menu, page API)
      setPinned(pin(2)),                              // a panel's, relayed
      pinWrites.setPinned(pin(3)),
      setPinned(pin(4)),
    ]);
  } finally { routeStoreWrites(null); }
  assert.deepEqual((await loadPins()).map((e) => e.source).sort(),
    [1, 2, 3, 4].map((n) => `https://cdn/${n}.png`));
});

test('a relayed ledger write returns the worker\'s record', async () => {
  install();
  routeStoreWrites((m) => handle(m));
  let rec;
  try {
    rec = await recordOpened({ source: 'https://cdn/x.png', name: 'x.png', editorUrl: 'http://localhost:8080/' });
  } finally { routeStoreWrites(null); }
  assert.equal(rec.count, 1);
  assert.equal((await loadLedger())[0].source, 'https://cdn/x.png');
});

test('a page, an unknown store and an unknown op are refused', async () => {
  install();
  const page = await handle({ store: 'pins', op: 'setPinned', args: [pin(1)] }, { url: 'https://evil.example/' });
  assert.equal(page.ok, false);
  assert.equal((await handle({ store: 'settings', op: 'setPinned', args: [] })).ok, false);
  assert.equal((await handle({ store: 'pins', op: 'constructor', args: [] })).ok, false);
  assert.deepEqual(await loadPins(), []);
});

test('no answering worker falls back to the document\'s own chain', async () => {
  install();
  routeStoreWrites(async () => undefined);
  try { await setPinned(pin(1)); } finally { routeStoreWrites(null); }
  assert.equal((await loadPins()).length, 1);
});
