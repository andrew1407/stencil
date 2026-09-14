// Unit tests for the extension's ported "Open in…" deep-link builders (src/lib/openIn.js).
// These are a PORT of browser/js/core/deepLink.js — the assertions below are the SAME
// expectations (and the SAME Telegram golden vectors) as browser/tests/deepLink.test.js,
// so the extension's stencil:// and t.me links stay byte-compatible with the browser app's,
// the desktop app (deepLink.cpp), and the Telegram bot (DeepLinkCodec.cs). Keep them in sync.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  buildStencilSchemeUrl, encodeTelegramStartPayload, buildTelegramLink,
  TELEGRAM_START_LIMIT, INLINE_WARN_CHARS, INLINE_MAX_CHARS,
} from '../src/lib/openIn.js';

// ── stencil:// scheme URLs (desktop inbound) ─────────────────────

test('buildStencilSchemeUrl builds a server-project link (server+id beat src)', () => {
  const url = buildStencilSchemeUrl({
    server: 'http://localhost:8090', id: 'p_1a2b3c_x1', version: 7,
    src: 'https://ignored.example/img.png', incognito: true,
  });
  assert.equal(
    url,
    'stencil://open?server=http%3A%2F%2Flocalhost%3A8090&id=p_1a2b3c_x1&version=7&incognito=1'
  );
});

test('buildStencilSchemeUrl builds an inline image link with layout + frame', () => {
  const url = buildStencilSchemeUrl({ src: 'https://x.example/a.png', layout: { lines: [] }, frame: 3 });
  assert.equal(
    url,
    'stencil://open?src=https%3A%2F%2Fx.example%2Fa.png&layout=%7B%22lines%22%3A%5B%5D%7D&frame=3'
  );
});

test('buildStencilSchemeUrl accepts a pre-serialized layout string and omits empty fields', () => {
  const url = buildStencilSchemeUrl({ src: 'a.png', layout: '{"lines":[]}' });
  assert.equal(url, 'stencil://open?src=a.png&layout=%7B%22lines%22%3A%5B%5D%7D');
});

test('buildStencilSchemeUrl honours a custom scheme (operator config)', () => {
  assert.equal(
    buildStencilSchemeUrl({ scheme: 'stencil-dev', src: 'a.png' }),
    'stencil-dev://open?src=a.png'
  );
});

test('buildStencilSchemeUrl embeds a data: URL inline (the extension hand-off path)', () => {
  const url = buildStencilSchemeUrl({ src: 'data:image/png;base64,AAA', incognito: true });
  assert.equal(url, 'stencil://open?src=data%3Aimage%2Fpng%3Bbase64%2CAAA&incognito=1');
});

// ── Telegram start-payload codec ─────────────────────────────────
// GOLDEN VECTORS — identical to browser/tests/deepLink.test.js (which duplicates
// desktop/tests/deepLink.headless.cpp + bot DeepLinkCodecTests.cs). Keep the four in sync.

const TELEGRAM_VECTORS = [
  ['localhost:8090', 'p_1a2b3c_x1', '1bG9jYWxob3N0OjgwOTB8cF8xYTJiM2NfeDE'],
  ['stencil.example.com', 'p_1a2b3c_x1', '1c3RlbmNpbC5leGFtcGxlLmNvbXxwXzFhMmIzY194MQ'],
  ['http://stencil.example.com', 'p_1', '1aHR0cDovL3N0ZW5jaWwuZXhhbXBsZS5jb218cF8x'],
  ['https://stencil.example.com:8443', 'p_1', '1c3RlbmNpbC5leGFtcGxlLmNvbTo4NDQzfHBfMQ'],
  ['https://hoooooooooooooooooooooooooooooooooooooooooo', 'p_1',
    '1aG9vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb3xwXzE'],
];

test('encodeTelegramStartPayload matches the golden vectors', () => {
  for (const [url, id, expected] of TELEGRAM_VECTORS) {
    assert.equal(encodeTelegramStartPayload(url, id), expected, `${url} | ${id}`);
    assert.ok(expected.length <= TELEGRAM_START_LIMIT);
    assert.match(expected, /^1[A-Za-z0-9_-]+$/, 'telegram-safe charset');
  }
});

test('encodeTelegramStartPayload returns null past the 64-char limit', () => {
  const host = 'https://h' + 'o'.repeat(43);   // 48 plaintext bytes → 65 payload chars → overflow
  assert.equal(encodeTelegramStartPayload(host, 'p_1'), null);
});

test('buildTelegramLink composes the t.me deep link', () => {
  assert.equal(buildTelegramLink('stencil_bot', '1abc'), 'https://t.me/stencil_bot?start=1abc');
});

// ── Inline size guards (mirror browser/js/ui/openInModal.js) ─────

test('inline size guards match the browser app', () => {
  assert.equal(INLINE_WARN_CHARS, 200_000);
  assert.equal(INLINE_MAX_CHARS, 1_000_000);
});
