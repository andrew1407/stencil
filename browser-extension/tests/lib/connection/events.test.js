// lib/connection/events.js — the server's global project feed over /ws, driven by a stand-in
// WebSocket. The token travels in the hello frame, so it never lands in a URL or a log line.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { eventsUrl, subscribeProjectEvents } from '../../../src/lib/connection/events.js';

const fakeSocket = () => {
  const made = [];
  class FakeWs {
    constructor(url) { this.url = url; this.sent = []; this.on = {}; this.closed = false; made.push(this); }
    addEventListener(t, fn) { (this.on[t] ||= []).push(fn); }
    send(m) { this.sent.push(m); }
    close() { this.closed = true; }
    fire(t, ev = {}) { for (const fn of this.on[t] || []) fn(ev); }
  }
  return { FakeWs, made };
};

test('the feed lives at /ws on the server origin, ws for http and wss for https', () => {
  assert.equal(eventsUrl('http://127.0.0.1:8090'), 'ws://127.0.0.1:8090/ws');
  assert.equal(eventsUrl('https://stencil.example'), 'wss://stencil.example/ws');
});

test('the hello names no project (the global feed) and carries the token', () => {
  const { FakeWs, made } = fakeSocket();
  const states = [];
  subscribeProjectEvents({ url: 'https://s.example', token: 'sekrit' }, () => {},
    { WebSocket: FakeWs, onState: (s) => states.push(s) });
  const [ws] = made;
  assert.ok(!ws.url.includes('sekrit'), 'no token in the URL');
  ws.fire('open');
  assert.deepEqual(JSON.parse(ws.sent[0]), { type: 'hello', token: 'sekrit' });
  assert.deepEqual(states, [true]);
});

test('only project-event frames reach the callback; junk is dropped', () => {
  const { FakeWs, made } = fakeSocket();
  const got = [];
  subscribeProjectEvents({ url: 'http://s:1', token: 't' }, (m) => got.push(m.event), { WebSocket: FakeWs });
  const [ws] = made;
  ws.fire('message', { data: '{"type":"project-event","event":"updated","project":{"id":"p1"}}' });
  ws.fire('message', { data: '{"type":"error","code":"unauthorized"}' });
  ws.fire('message', { data: 'not json' });
  assert.deepEqual(got, ['updated']);
});

test('a server-side drop reports once; our own close() reports nothing', () => {
  const { FakeWs, made } = fakeSocket();
  const states = [];
  const opts = { WebSocket: FakeWs, onState: (s) => states.push(s) };
  subscribeProjectEvents({ url: 'http://s:1', token: 't' }, () => {}, opts);
  made[0].fire('close');
  made[0].fire('close');
  assert.deepEqual(states, [false]);
  const sub = subscribeProjectEvents({ url: 'http://s:1', token: 't' }, () => {}, opts);
  sub.close();
  made[1].fire('close');
  assert.ok(made[1].closed);
  assert.deepEqual(states, [false]);
});

test('no WebSocket, or a constructor that throws, is simply no feed', () => {
  assert.equal(subscribeProjectEvents({ url: 'http://s:1', token: 't' }, () => {}, { WebSocket: null }), null);
  class Refuses { constructor() { throw new Error('blocked'); } }
  assert.equal(subscribeProjectEvents({ url: 'http://s:1', token: 't' }, () => {}, { WebSocket: Refuses }), null);
});
