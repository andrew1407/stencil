import { test } from 'node:test';
import assert from 'node:assert';

import {
  OPEN_PARAM, readOpenProjectId, buildOpenProjectUrl, buildExternalLaunchUrl,
  buildStencilSchemeUrl, encodeTelegramStartPayload, buildTelegramLink,
  buildDesktopBounceUrl, normalizeLaunchPayload, TELEGRAM_START_LIMIT,
} from '../js/core/deepLink.js';

test('readOpenProjectId returns the project id from the open param', () => {
  assert.strictEqual(readOpenProjectId('?open=p_1'), 'p_1');
  assert.strictEqual(readOpenProjectId('?open=p_1&x=2'), 'p_1');
  assert.strictEqual(readOpenProjectId('?x=2&open=abc'), 'abc');
});

test('readOpenProjectId decodes URL-encoded ids', () => {
  assert.strictEqual(readOpenProjectId('?open=p%20a'), 'p a');
});

test('readOpenProjectId returns null when absent or empty', () => {
  assert.strictEqual(readOpenProjectId(''), null);
  assert.strictEqual(readOpenProjectId('?x=2'), null);
  assert.strictEqual(readOpenProjectId('?open='), null);
  assert.strictEqual(readOpenProjectId(), null);
});

test('buildOpenProjectUrl appends the encoded open param', () => {
  assert.strictEqual(
    buildOpenProjectUrl('https://app.example/editor', 'p_1'),
    'https://app.example/editor?open=p_1'
  );
  assert.strictEqual(
    buildOpenProjectUrl('https://app.example/editor', 'p a'),
    'https://app.example/editor?open=p%20a'
  );
});

test('buildOpenProjectUrl output round-trips through readOpenProjectId', () => {
  for (const id of ['p_1', 'abc-123', 'weird id&=?']) {
    const url = buildOpenProjectUrl('https://x/y', id);
    const search = url.slice(url.indexOf('?'));
    assert.strictEqual(readOpenProjectId(search), id, `round-trip ${id}`);
  }
});

test('OPEN_PARAM is the documented param name', () => {
  assert.strictEqual(OPEN_PARAM, 'open');
});

test('buildExternalLaunchUrl encodes the payload into the #stencil= fragment', () => {
  const url = buildExternalLaunchUrl('https://app.example/editor', { dataUrl: 'data:image/png;base64,AAA', name: 'a.png', incognito: true });
  assert.ok(url.startsWith('https://app.example/editor#stencil='), 'has #stencil= fragment');
  // The receiver (applyExternalLaunch) does: JSON.parse(decodeURIComponent(hash.slice('#stencil='.length)))
  const json = decodeURIComponent(url.slice(url.indexOf('#stencil=') + '#stencil='.length));
  const payload = JSON.parse(json);
  assert.deepStrictEqual(payload, { dataUrl: 'data:image/png;base64,AAA', name: 'a.png', incognito: true });
});

// ── stencil:// scheme URLs (desktop inbound) ─────────────────────

test('buildStencilSchemeUrl builds a server-project link (server+id beat src)', () => {
  const url = buildStencilSchemeUrl({
    server: 'http://localhost:8090', id: 'p_1a2b3c_x1', version: 7,
    src: 'https://ignored.example/img.png', incognito: true,
  });
  assert.strictEqual(
    url,
    'stencil://open?server=http%3A%2F%2Flocalhost%3A8090&id=p_1a2b3c_x1&version=7&incognito=1'
  );
});

test('buildStencilSchemeUrl builds an inline image link with layout + frame', () => {
  const url = buildStencilSchemeUrl({ src: 'https://x.example/a.png', layout: { lines: [] }, frame: 3 });
  assert.strictEqual(
    url,
    'stencil://open?src=https%3A%2F%2Fx.example%2Fa.png&layout=%7B%22lines%22%3A%5B%5D%7D&frame=3'
  );
});

test('buildStencilSchemeUrl accepts a pre-serialized layout string and omits empty fields', () => {
  const url = buildStencilSchemeUrl({ src: 'a.png', layout: '{"lines":[]}' });
  assert.strictEqual(url, 'stencil://open?src=a.png&layout=%7B%22lines%22%3A%5B%5D%7D');
});

// ── Telegram start-payload codec ─────────────────────────────────
// GOLDEN VECTORS — duplicated verbatim in desktop/tests/deepLink.headless.cpp and
// bot/tests/Stencil.TelegramBot.Tests/DeepLinkCodecTests.cs. Keep the three in sync.

const TELEGRAM_VECTORS = [
  // loopback keeps http by default → scheme dropped, host|id encoded
  ['localhost:8090', 'p_1a2b3c_x1', '1bG9jYWxob3N0OjgwOTB8cF8xYTJiM2NfeDE'],
  // bare remote host defaults to https → scheme dropped
  ['stencil.example.com', 'p_1a2b3c_x1', '1c3RlbmNpbC5leGFtcGxlLmNvbXxwXzFhMmIzY194MQ'],
  // explicit http on a remote host is NOT the default → full origin kept
  ['http://stencil.example.com', 'p_1', '1aHR0cDovL3N0ZW5jaWwuZXhhbXBsZS5jb218cF8x'],
  // https on a remote host IS the default → dropped, port kept
  ['https://stencil.example.com:8443', 'p_1', '1c3RlbmNpbC5leGFtcGxlLmNvbTo4NDQzfHBfMQ'],
  // 47 plaintext bytes → exactly 64 payload chars (the boundary)
  ['https://hoooooooooooooooooooooooooooooooooooooooooo', 'p_1',
    '1aG9vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb3xwXzE'],
];

test('encodeTelegramStartPayload matches the golden vectors', () => {
  for (const [url, id, expected] of TELEGRAM_VECTORS) {
    assert.strictEqual(encodeTelegramStartPayload(url, id), expected, `${url} | ${id}`);
    assert.ok(expected.length <= TELEGRAM_START_LIMIT);
    assert.match(expected, /^1[A-Za-z0-9_-]+$/, 'telegram-safe charset');
  }
});

test('encodeTelegramStartPayload returns null past the 64-char limit', () => {
  // 48 plaintext bytes → 65 payload chars → overflow
  const host = 'https://h' + 'o'.repeat(43);
  assert.strictEqual(encodeTelegramStartPayload(host, 'p_1'), null);
});

test('buildTelegramLink composes the t.me deep link', () => {
  assert.strictEqual(buildTelegramLink('stencil_bot', '1abc'), 'https://t.me/stencil_bot?start=1abc');
});

// ── Desktop bounce URL (Telegram cannot linkify stencil://) ──────

test('buildDesktopBounceUrl wraps a stencil:// URL for launch.html', () => {
  const stencilUrl = 'stencil://open?server=http%3A%2F%2Flocalhost%3A8090&id=p_1';
  assert.strictEqual(
    buildDesktopBounceUrl('http://localhost:8080/', stencilUrl),
    'http://localhost:8080/launch.html#stencil-desktop=' + encodeURIComponent(stencilUrl)
  );
});

// ── normalizeLaunchPayload (inbound #stencil= schema) ────────────

test('normalizeLaunchPayload keeps the legacy dataUrl-only payload valid', () => {
  const p = normalizeLaunchPayload({ dataUrl: 'data:image/png;base64,AAA', name: 'a.png', incognito: true });
  assert.strictEqual(p.kind, 'dataUrl');
  assert.strictEqual(p.dataUrl, 'data:image/png;base64,AAA');
  assert.strictEqual(p.name, 'a.png');
  assert.strictEqual(p.incognito, true);
  assert.strictEqual(p.layout, null);
});

test('normalizeLaunchPayload carries noCrop through (Open-Image "new tab" Crop-off)', () => {
  assert.strictEqual(normalizeLaunchPayload({ dataUrl: 'data:image/png;base64,AAA', noCrop: true }).noCrop, true);
  assert.strictEqual(normalizeLaunchPayload({ dataUrl: 'data:image/png;base64,AAA' }).noCrop, false);
});

test('normalizeLaunchPayload accepts a server reference without image bytes', () => {
  const p = normalizeLaunchPayload({ server: { url: 'localhost:8090', id: 'p_1', version: 4 } });
  assert.strictEqual(p.kind, 'server');
  assert.deepStrictEqual(p.server, { url: 'localhost:8090', id: 'p_1', version: 4 });
  assert.strictEqual(p.incognito, false);
});

test('normalizeLaunchPayload defaults a missing/garbage server version to 0', () => {
  assert.strictEqual(normalizeLaunchPayload({ server: { url: 'x', id: 'p' } }).server.version, 0);
  assert.strictEqual(normalizeLaunchPayload({ server: { url: 'x', id: 'p', version: 'nope' } }).server.version, 0);
});

test('normalizeLaunchPayload prefers server over dataUrl over src', () => {
  const all = {
    server: { url: 'x', id: 'p_1' },
    dataUrl: 'data:image/png;base64,AAA',
    src: 'https://x.example/a.png',
  };
  assert.strictEqual(normalizeLaunchPayload(all).kind, 'server');
  delete all.server;
  assert.strictEqual(normalizeLaunchPayload(all).kind, 'dataUrl');
  delete all.dataUrl;
  assert.strictEqual(normalizeLaunchPayload(all).kind, 'src');
});

test('normalizeLaunchPayload carries the layout object through', () => {
  const layout = { imageWidth: 10, imageHeight: 20, lines: [] };
  const p = normalizeLaunchPayload({ src: 'https://x.example/a.png', layout });
  assert.strictEqual(p.kind, 'src');
  assert.deepStrictEqual(p.layout, layout);
});

test('normalizeLaunchPayload: __proto__ in layout/crop/page is stripped, never pollutes', () => {
  // A crafted #stencil= payload whose object sub-fields carry prototype-pollution keys.
  const evil = JSON.parse(
    '{"src":"https://x.example/a.png","layout":{"__proto__":{"polluted":1},"lines":[]},'
    + '"crop":{"constructor":{"x":1},"x1":0},"page":{"__proto__":{"y":1}}}'
  );
  const p = normalizeLaunchPayload(evil);
  assert.strictEqual(({}).polluted, undefined);   // Object.prototype untouched
  assert.strictEqual(({}).x, undefined);
  assert.strictEqual(({}).y, undefined);
  // The cleaned sub-objects keep only safe keys.
  assert.deepStrictEqual(Object.keys(p.layout), ['lines']);
  assert.deepStrictEqual(Object.keys(p.crop), ['x1']);
});

test('normalizeLaunchPayload rejects junk', () => {
  assert.strictEqual(normalizeLaunchPayload(null), null);
  assert.strictEqual(normalizeLaunchPayload('x'), null);
  assert.strictEqual(normalizeLaunchPayload({}), null);
  assert.strictEqual(normalizeLaunchPayload({ src: 'ftp://x/a.png' }), null);        // non-http(s) src
  assert.strictEqual(normalizeLaunchPayload({ server: { url: 'x' } }), null);        // id missing
  assert.strictEqual(normalizeLaunchPayload({ server: { id: 'p_1' } }), null);       // url missing
});

test('normalizeLaunchPayload rejects a non-data: dataUrl (no http/javascript smuggling)', () => {
  assert.strictEqual(normalizeLaunchPayload({ dataUrl: 'http://169.254.169.254/latest/' }), null);
  assert.strictEqual(normalizeLaunchPayload({ dataUrl: 'javascript:alert(1)' }), null);
  assert.strictEqual(normalizeLaunchPayload({ dataUrl: 'file:///etc/passwd' }), null);
});
