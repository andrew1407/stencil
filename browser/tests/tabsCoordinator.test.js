import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert';
import { MSG } from '../js/worker/messages.js';
import { TabsCoordinator } from '../js/core/tabsCoordinator.js';

// TabsCoordinator picks the SharedWorker transport when one exists. We stub a
// fake worker + a minimal `window` so construction takes that path in Node, then
// drive incoming worker messages through the captured port to exercise the
// subscribe / emit / unsubscribe registry without any real worker or channel.
class FakePort {
  constructor() { this.sent = []; this.onmessage = null; }
  start() {}
  postMessage(msg) { this.sent.push(msg); }
  deliver(data) { if (this.onmessage) this.onmessage({ data }); }
}

class FakeSharedWorker {
  constructor() { this.port = new FakePort(); FakeSharedWorker.last = this; }
}

let savedSharedWorker;
let savedWindow;

beforeEach(() => {
  savedSharedWorker = globalThis.SharedWorker;
  savedWindow = globalThis.window;
  globalThis.SharedWorker = FakeSharedWorker;
  // #trySharedWorker wires a `beforeunload` listener and projectsChanged()
  // dispatches a DOM event — both best-effort, so a no-op window suffices.
  globalThis.window = { addEventListener() {}, dispatchEvent() {} };
});

afterEach(() => {
  if (savedSharedWorker === undefined) delete globalThis.SharedWorker;
  else globalThis.SharedWorker = savedSharedWorker;
  if (savedWindow === undefined) delete globalThis.window;
  else globalThis.window = savedWindow;
});

const port = () => FakeSharedWorker.last.port;

test('uses the SharedWorker transport and says hello', () => {
  new TabsCoordinator();
  assert.deepEqual(port().sent, [{ type: MSG.HELLO }]);
});

test('onTabCount fires with the latest count from a TABCOUNT message', () => {
  const tabs = new TabsCoordinator();
  let got = null;
  tabs.onTabCount(c => { got = c; });
  port().deliver({ type: MSG.TABCOUNT, count: 3, youAreOnly: false });
  assert.deepEqual(got, { count: 3, youAreOnly: false });
});

test('onPeers / onAccent / onIncognitoPeers each receive their own payload', () => {
  const tabs = new TabsCoordinator();
  let peers;
  let accent;
  let incog;
  tabs.onPeers(ids => { peers = ids; });
  tabs.onAccent(key => { accent = key; });
  tabs.onIncognitoPeers(list => { incog = list; });

  port().deliver({ type: MSG.PEERS, activeIds: ['a', 'b'] });
  port().deliver({ type: MSG.ACCENT, key: 'rose' });
  port().deliver({ type: MSG.INCOGNITOS, sessions: [{ name: 'x', updatedAt: 1 }] });

  assert.deepEqual(peers, ['a', 'b']);
  assert.equal(accent, 'rose');
  assert.deepEqual(incog, [{ name: 'x', updatedAt: 1 }]);
});

test('onProjectsChanged receives the full PROJECTS_CHANGED detail', () => {
  const tabs = new TabsCoordinator();
  let detail = null;
  tabs.onProjectsChanged(d => { detail = d; });
  port().deliver({ type: MSG.PROJECTS_CHANGED, action: 'updated', id: 'p1' });
  assert.equal(detail.action, 'updated');
  assert.equal(detail.id, 'p1');
});

test('every subscriber on a channel is notified', () => {
  const tabs = new TabsCoordinator();
  const hits = [];
  tabs.onAccent(() => hits.push('a'));
  tabs.onAccent(() => hits.push('b'));
  port().deliver({ type: MSG.ACCENT, key: 'teal' });
  assert.deepEqual(hits, ['a', 'b']);
});

test('the unsubscribe returned by onX stops further delivery', () => {
  const tabs = new TabsCoordinator();
  let n = 0;
  const off = tabs.onAccent(() => { n++; });
  port().deliver({ type: MSG.ACCENT, key: 'one' });
  off();
  port().deliver({ type: MSG.ACCENT, key: 'two' });
  assert.equal(n, 1);
});

test('a throwing subscriber is isolated so the others still fire', () => {
  const tabs = new TabsCoordinator();
  const hits = [];
  tabs.onTabCount(() => hits.push('first'));
  tabs.onTabCount(() => { throw new Error('boom'); });
  tabs.onTabCount(() => hits.push('third'));
  assert.doesNotThrow(() => port().deliver({ type: MSG.TABCOUNT, count: 1, youAreOnly: true }));
  assert.deepEqual(hits, ['first', 'third']);
});

test('channels are isolated — a PEERS message does not fire tabCount subscribers', () => {
  const tabs = new TabsCoordinator();
  let tabCount = 0;
  let peers = 0;
  tabs.onTabCount(() => { tabCount++; });
  tabs.onPeers(() => { peers++; });
  port().deliver({ type: MSG.PEERS, activeIds: [] });
  assert.equal(peers, 1);
  assert.equal(tabCount, 0);
});

test('outgoing broadcastAccent posts an ACCENT control message to the worker', () => {
  const tabs = new TabsCoordinator();
  port().sent.length = 0; // drop the initial HELLO
  tabs.broadcastAccent('amber');
  assert.deepEqual(port().sent, [{ type: MSG.ACCENT, key: 'amber' }]);
});
