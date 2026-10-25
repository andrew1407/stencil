// js/worker/projectsWorker.js: the port table behind the tab count and the peer list. A tab that
// said BYE (pagehide) and came back from the bfcache says HELLO again and is counted again; one
// that crashed is dropped once it misses its pings, and the ping timer stops with the last port.
import { test, mock } from 'node:test';
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

test('a tab that stops answering pings is dropped; one that answers stays; a thawed one rejoins', () => {
  mock.timers.enable({ apis: ['setInterval'] });
  const live = mock.method(globalThis, 'clearInterval');
  try {
    const crashed = connect();
    const alive = connect();
    crashed.deliver({ type: MSG.ACTIVE, activeId: 'gone' });
    for (let i = 0; i < 4; i++) {
      mock.timers.tick(10_000);
      alive.deliver({ type: MSG.PONG, activeId: null, incognito: null });
    }
    assert.equal(alive.last(MSG.PING).type, MSG.PING);
    assert.deepEqual(alive.last(MSG.TABCOUNT), { type: MSG.TABCOUNT, count: 1, youAreOnly: true });
    assert.deepEqual(alive.last(MSG.PEERS).activeIds, []);
    // A frozen tab's queued pings land on thaw: its PONG brings it back with its state.
    crashed.deliver({ type: MSG.PONG, activeId: 'back', incognito: null });
    assert.equal(alive.last(MSG.TABCOUNT).count, 2);
    assert.deepEqual(alive.last(MSG.PEERS).activeIds, ['back']);
    crashed.deliver({ type: MSG.BYE });
    // Any other word from a dropped port is answered with a ping, so it PONGs its way back.
    const pings = crashed.sent.length;
    crashed.deliver({ type: MSG.ACTIVE, activeId: 'x' });
    assert.equal(crashed.sent.length, pings + 1);
    assert.equal(crashed.sent.at(-1).type, MSG.PING);
    assert.equal(live.mock.callCount(), 0);
    alive.deliver({ type: MSG.BYE });
    assert.equal(live.mock.callCount(), 1, 'the last port out stops the ping timer');
  } finally {
    live.mock.restore();
    mock.timers.reset();
  }
});
