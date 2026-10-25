// The extension modal's hand-off (js/prePaintTheme.js): a frame opened on `#stencil-await` tells its
// parent it is ready, and takes the `#stencil=` payload posted back by that parent only, into the
// fragment js/core/launch/controller.js reads — the frame's src, which the host page can read, never carries it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const SRC = readFileSync(new URL('../../../js/prePaintTheme.js', import.meta.url), 'utf8');

const frame = (hash, { late = false } = {}) => {
  const listeners = new Set();
  const told = [];
  const reloads = [];
  const parent = { postMessage: (data, target) => told.push({ data, target }) };
  const location = { hash, pathname: '/', search: '?x=1', reload: () => reloads.push(location.hash) };
  const window = {
    parent, location, matchMedia: () => ({ matches: false }),
    addEventListener: (t, fn) => { if (t === 'message') listeners.add(fn); },
    removeEventListener: (t, fn) => { if (t === 'message') listeners.delete(fn); },
  };
  const history = { replaceState: (_s, _t, url) => { location.hash = url.slice(url.indexOf('#')); } };
  vm.runInNewContext(SRC, {
    window, location, history, URL,
    document: {
      referrer: 'https://host.example/page',
      documentElement: { setAttribute() {}, hasAttribute: (a) => late && a === 'data-handoff-late' },
    },
    localStorage: { getItem: () => null },
  });
  const post = (data, source = parent) => [...listeners].forEach((fn) => fn({ data, source }));
  return { told, post, location, listeners, reloads };
};

const HANDOFF = { source: 'stencil-extension', type: 'handoff', hash: '#stencil=%7B%7D' };

test('a frame awaiting its payload says ready to the referrer\'s origin, and takes it from the parent', () => {
  const f = frame('#stencil-await');
  assert.deepEqual(JSON.parse(JSON.stringify(f.told)), [{ data: { source: 'stencil-modal', type: 'ready' }, target: 'https://host.example' }]);
  f.post(HANDOFF);
  assert.equal(f.location.hash, '#stencil=%7B%7D');
  assert.equal(f.listeners.size, 0, 'one hand-off, then the frame stops listening');
  assert.deepEqual(f.reloads, [], 'a payload in time is read by the booting app');
});

test('a payload landing after the app booted empty reloads the frame onto it', () => {
  const f = frame('#stencil-await', { late: true });
  f.post(HANDOFF);
  assert.deepEqual(f.reloads, ['#stencil=%7B%7D']);
});

test('a payload from anyone but the parent, or not a #stencil= fragment, is ignored', () => {
  const f = frame('#stencil-await');
  f.post(HANDOFF, { other: true });
  f.post({ ...HANDOFF, source: 'stencil-modal' });
  f.post({ ...HANDOFF, hash: '#open=https://evil.example/x.png' });
  f.post({ ...HANDOFF, hash: 42 });
  assert.equal(f.location.hash, '#stencil-await');
});

test('a frame opened with its payload in the fragment still says ready, and listens for none', () => {
  const f = frame('#stencil=%7B%7D');
  assert.equal(f.told.length, 1);
  assert.equal(f.listeners.size, 0);
  assert.equal(frame('#other').told.length, 0);
});
