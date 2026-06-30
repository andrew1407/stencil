import { test } from 'node:test';
import assert from 'node:assert';
import { Emitter } from '../js/core/emitter.js';

test('emit notifies every subscriber on a channel with the args', () => {
  const bus = new Emitter();
  const seen = [];
  bus.on('x', (...a) => seen.push(['a', ...a]));
  bus.on('x', (...a) => seen.push(['b', ...a]));
  bus.emit('x', 1, 2);
  assert.deepEqual(seen, [['a', 1, 2], ['b', 1, 2]]);
});

test('channels are isolated — emit only reaches its own channel', () => {
  const bus = new Emitter();
  let x = 0;
  let y = 0;
  bus.on('x', () => { x++; });
  bus.on('y', () => { y++; });
  bus.emit('x');
  assert.equal(x, 1);
  assert.equal(y, 0);
});

test('emit on an unknown channel is a no-op', () => {
  const bus = new Emitter();
  assert.doesNotThrow(() => bus.emit('nobody-home', 42));
});

test('on() returns an unsubscribe that stops further delivery', () => {
  const bus = new Emitter();
  let n = 0;
  const off = bus.on('x', () => { n++; });
  bus.emit('x');
  off();
  bus.emit('x');
  assert.equal(n, 1);
});

test('a throwing subscriber is isolated so the rest still fire', () => {
  const bus = new Emitter();
  const order = [];
  bus.on('x', () => { order.push('first'); });
  bus.on('x', () => { throw new Error('boom'); });
  bus.on('x', () => { order.push('third'); });
  assert.doesNotThrow(() => bus.emit('x'));
  assert.deepEqual(order, ['first', 'third']);
});

test('the same callback added once fires once (Set-backed)', () => {
  const bus = new Emitter();
  let n = 0;
  const cb = () => { n++; };
  bus.on('x', cb);
  bus.on('x', cb);
  bus.emit('x');
  assert.equal(n, 1);
});

test('clear() drops every subscriber on every channel', () => {
  const bus = new Emitter();
  let n = 0;
  bus.on('x', () => { n++; });
  bus.on('y', () => { n++; });
  bus.clear();
  bus.emit('x');
  bus.emit('y');
  assert.equal(n, 0);
});

test('unsubscribing one subscriber leaves the others intact', () => {
  const bus = new Emitter();
  let a = 0;
  let b = 0;
  const offA = bus.on('x', () => { a++; });
  bus.on('x', () => { b++; });
  offA();
  bus.emit('x');
  assert.equal(a, 0);
  assert.equal(b, 1);
});
