// The "Copy Stencil link" URL (src/lib/menu/shareLink.js): the launch URI's `source`, and `incognito`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { shareLink } from '../../../src/lib/menu/shareLink.js';

const BASE = 'https://andrew1407.github.io/stencil/';

test('a link opens the app at its base on the source, encoded once', () => {
  const src = 'https://cdn.example.com/a b/pic.png?w=200&h=100#x';
  const link = shareLink(BASE, src);
  assert.equal(link, `${BASE}?source=${encodeURIComponent(src).replace(/%20/g, '+')}`);
  assert.equal(new URL(link).searchParams.get('source'), src);
  assert.equal(new URL(link).searchParams.has('incognito'), false);
});

test('the incognito link adds the flag the app reads', () => {
  const u = new URL(shareLink(BASE, 'https://x.test/v.mp4', { incognito: true }));
  assert.deepEqual([u.searchParams.get('source'), u.searchParams.get('incognito')], ['https://x.test/v.mp4', 'true']);
});

test('any http(s) base: its own path and query are kept', () => {
  assert.equal(new URL(shareLink('http://localhost:8080/', 'http://h/p.png')).origin, 'http://localhost:8080');
  const u = new URL(shareLink('https://me.example/stencil/index.html?theme=dark', 'https://h/p.png'));
  assert.equal(u.pathname, '/stencil/index.html');
  assert.equal(u.searchParams.get('theme'), 'dark');
});

test('nothing another browser cannot open: a blob:, data: or file: source, or a non-http base', () => {
  for (const src of ['blob:https://x/1', 'data:image/png;base64,AA', 'file:///etc/x.png', '', null])
    assert.equal(shareLink(BASE, src), null, String(src));
  for (const base of ['chrome-extension://abc/', 'javascript:x', 'nope'])
    assert.equal(shareLink(base, 'https://h/p.png'), null, base);
});
