// js/worker/projectsWorker.js: the port table behind the tab count and the peer list. A tab that
// said BYE (pagehide) and came back from the bfcache says HELLO again and is counted again.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MSG } from '../../js/worker/messages.js';

// The SharedWorker global the module installs its onconnect on.
globalThis.self = {};
await import('../../js/worker/projectsWorker.js');

class MockPort {
  constructor() { this.sent = []; this.onmessage = null; }
  start() {}
  postMessage(msg) { this.sent.push(msg); }
  deliver(data) { this.onmessage({ data }); }
  last(type) { return this.sent.filter((m) => m.type === type).at(-1); }
}
const connect = () => { const port = new MockPort(); globalThis.self.onconnect({ ports: [port] }); port.deliver({ type: MSG.HELLO }); return port; };

test('HELLO re-adds a port that said BYE, with the state the tab carries', () => {
  const a = connect();
  const b = connect();
  a.deliver({ type: MSG.ACTIVE, activeId: 'p1' });
  assert.deepEqual(b.last(MSG.TABCOUNT), { type: MSG.TABCOUNT, count: 2, youAreOnly: false });
  a.deliver({ type: MSG.BYE });
  assert.deepEqual(b.last(MSG.TABCOUNT), { type: MSG.TABCOUNT, count: 1, youAreOnly: true });
  assert.deepEqual(b.last(MSG.PEERS).activeIds, []);
  a.deliver({ type: MSG.HELLO, activeId: 'p1', incognito: null });
  assert.deepEqual(b.last(MSG.TABCOUNT), { type: MSG.TABCOUNT, count: 2, youAreOnly: false });
  assert.deepEqual(b.last(MSG.PEERS).activeIds, ['p1']);
  assert.deepEqual(a.last(MSG.TABCOUNT), { type: MSG.TABCOUNT, count: 2, youAreOnly: false });
  // A repeated HELLO from a counted tab neither doubles it nor forgets its project.
  a.deliver({ type: MSG.HELLO });
  assert.equal(b.last(MSG.TABCOUNT).count, 2);
  assert.deepEqual(b.last(MSG.PEERS).activeIds, ['p1']);
  a.deliver({ type: MSG.BYE });
  b.deliver({ type: MSG.BYE });
});
