// Tests for src/lib/connection/urlGuard.js — the lexical SSRF guard on page-harvested fetch
// URLs. Fetches carry the extension's <all_urls> host permissions, so every
// private/internal literal class a page could name must be refused.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isAllowedImageUrl } from '../../../src/lib/connection/urlGuard.js';

test('public http(s) URLs are allowed', () => {
  for (const url of [
    'https://example.com/cat.png',
    'http://cdn.example.org:8080/a/b.jpg?x=1#f',
    'https://8.8.8.8/img.png',
    'https://[2606:4700::6810:84e5]/img.png',   // public IPv6 literal
  ]) assert.equal(isAllowedImageUrl(url), true, url);
});

test('data: and blob: pass through (no network host)', () => {
  assert.equal(isAllowedImageUrl('data:image/png;base64,AAAA'), true);
  assert.equal(isAllowedImageUrl('blob:https://x.example/uuid'), true);
});

test('non-http(s) schemes are refused', () => {
  for (const url of [
    'file:///etc/passwd',
    'ftp://example.com/a.png',
    'javascript:alert(1)',
    'chrome://settings',
    'chrome-extension://abc/x.png',
    'not a url',
    '',
  ]) assert.equal(isAllowedImageUrl(url), false, url);
});

test('localhost names are blocked', () => {
  for (const url of [
    'http://localhost/x.png',
    'http://localhost:8080/x.png',
    'https://LOCALHOST/x.png',
    'http://foo.localhost/x.png',
    'http://localhost./x.png',
    'http://app.localhost./x.png',
  ]) assert.equal(isAllowedImageUrl(url), false, url);
});

test('a trailing-dot host is the same host: loopback unlocks it, a public name still passes', () => {
  assert.equal(isAllowedImageUrl('http://localhost./x.png', { allowLoopback: true }), true);
  assert.equal(isAllowedImageUrl('http://cdn.example./x.png'), true);
  assert.equal(isAllowedImageUrl('http://localhost./x.png', { allowSameHostAs: 'http://localhost/page' }), true);
  assert.equal(isAllowedImageUrl('http://127.0.0.1./x.png'), false, 'the URL parser drops the dot of an IPv4');
});

test('loopback literals are blocked (127/8, ::1, bracketed)', () => {
  for (const url of [
    'http://127.0.0.1/x.png',
    'http://127.1.2.3/x.png',
    'http://[::1]/x.png',
    'http://[::1]:9000/x.png',
  ]) assert.equal(isAllowedImageUrl(url), false, url);
});

test('WHATWG-normalised IPv4 spellings of loopback are blocked too', () => {
  // new URL() canonicalises these to 127.0.0.1 before the guard sees them.
  assert.equal(isAllowedImageUrl('http://0x7f.0.0.1/x.png'), false);
  assert.equal(isAllowedImageUrl('http://2130706433/x.png'), false);
});

test('private ranges are blocked (10/8, 172.16/12, 192.168/16)', () => {
  for (const url of [
    'http://10.0.0.5/x.png',
    'http://172.16.0.1/x.png',
    'http://172.31.255.255/x.png',
    'http://192.168.1.10/x.png',
  ]) assert.equal(isAllowedImageUrl(url), false, url);
  // Just outside 172.16/12 is public.
  assert.equal(isAllowedImageUrl('http://172.15.0.1/x.png'), true);
  assert.equal(isAllowedImageUrl('http://172.32.0.1/x.png'), true);
});

test('link-local and the metadata IP are blocked', () => {
  assert.equal(isAllowedImageUrl('http://169.254.169.254/latest/meta-data/'), false);
  assert.equal(isAllowedImageUrl('http://169.254.0.7/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[fe80::1]/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[FE80::a:b]/x.png'), false);
});

test('the reserved/documentation classes the CLI and desktop guards block are blocked too', () => {
  for (const host of ['192.0.0.1', '192.0.2.7', '198.18.0.1', '198.19.255.1', '198.51.100.4',
    '203.0.113.9', '224.0.0.1', '240.0.0.1', '255.255.255.255', '[fec0::1]']) {
    assert.equal(isAllowedImageUrl(`http://${host}/x.png`), false, host);
  }
});

test('CGNAT, ULA, and unspecified are blocked', () => {
  assert.equal(isAllowedImageUrl('http://100.64.0.1/x.png'), false);
  assert.equal(isAllowedImageUrl('http://100.127.9.9/x.png'), false);
  assert.equal(isAllowedImageUrl('http://100.63.0.1/x.png'), true);   // below CGNAT
  assert.equal(isAllowedImageUrl('http://100.128.0.1/x.png'), true);  // above CGNAT
  assert.equal(isAllowedImageUrl('http://[fc00::1]/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[fdab:1::2]/x.png'), false);
  assert.equal(isAllowedImageUrl('http://0.0.0.0/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[::]/x.png'), false);
});

test('IPv4-mapped IPv6 is checked as its embedded IPv4', () => {
  // new URL() re-serialises ::ffff:10.0.0.1 in hex-group form; both spellings block.
  assert.equal(isAllowedImageUrl('http://[::ffff:10.0.0.1]/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[::ffff:a00:1]/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[::ffff:127.0.0.1]/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[::ffff:808:808]/x.png'), true);   // 8.8.8.8
});

test('allowSameHostAs permits the scanned page\'s OWN host (any private class)', () => {
  const from = (page) => ({ allowSameHostAs: page });
  // Same host — port and path may differ (host is what the browser already talks to).
  assert.equal(isAllowedImageUrl('http://localhost:8080/x.png', from('http://localhost:9000/page')), true);
  assert.equal(isAllowedImageUrl('http://127.0.0.1/x.png', from('http://127.0.0.1:3000/a')), true);
  assert.equal(isAllowedImageUrl('http://10.0.0.5/x.png', from('https://10.0.0.5/index')), true);
  assert.equal(isAllowedImageUrl('http://192.168.1.10/x.png', from('http://192.168.1.10/')), true);
  assert.equal(isAllowedImageUrl('http://[fe80::1]/x.png', from('http://[fe80::1]/wiki')), true);
});

test('allowSameHostAs never unlocks a DIFFERENT private host', () => {
  const page = { allowSameHostAs: 'http://192.168.1.10/index.html' };
  assert.equal(isAllowedImageUrl('http://192.168.1.11/x.png', page), false);
  assert.equal(isAllowedImageUrl('http://10.0.0.5/x.png', page), false);
  assert.equal(isAllowedImageUrl('http://127.0.0.1/x.png', page), false);
  assert.equal(isAllowedImageUrl('http://localhost/x.png', page), false);
  // A public page lends nothing private either.
  assert.equal(isAllowedImageUrl('http://10.0.0.5/x.png', { allowSameHostAs: 'https://example.com/' }), false);
});

test('allowSameHostAs never unlocks the metadata IP — even from itself', () => {
  for (const page of ['http://169.254.169.254/', 'http://example.com/']) {
    assert.equal(isAllowedImageUrl('http://169.254.169.254/latest/meta-data/', { allowSameHostAs: page }), false);
    assert.equal(isAllowedImageUrl('http://[::ffff:169.254.169.254]/x', { allowSameHostAs: page }), false);
    assert.equal(isAllowedImageUrl('http://[::ffff:a9fe:a9fe]/x', { allowSameHostAs: page }), false);
  }
  // Other 169.254 link-local hosts do get the same-host carve-out.
  assert.equal(isAllowedImageUrl('http://169.254.0.7/x.png', { allowSameHostAs: 'http://169.254.0.7/' }), true);
});

test('allowSameHostAs ignores non-http(s) or unparseable page context', () => {
  for (const page of ['file:///tmp/page.html', 'chrome-extension://abc/popup.html', 'not a url', '']) {
    assert.equal(isAllowedImageUrl('http://localhost/x.png', { allowSameHostAs: page }), false, page);
  }
});

test('allowLoopback permits loopback ONLY (explicit-user URLs)', () => {
  const allow = { allowLoopback: true };
  assert.equal(isAllowedImageUrl('http://127.0.0.1:8080/x.png', allow), true);
  assert.equal(isAllowedImageUrl('http://localhost:8080/x.png', allow), true);
  assert.equal(isAllowedImageUrl('http://[::1]/x.png', allow), true);
  // Everything else stays blocked even for user-typed URLs.
  assert.equal(isAllowedImageUrl('http://10.0.0.5/x.png', allow), false);
  assert.equal(isAllowedImageUrl('http://169.254.169.254/x.png', allow), false);
  assert.equal(isAllowedImageUrl('http://[fe80::1]/x.png', allow), false);
});

// Each of these routes to the IPv4 it carries, so the IPv4 decides: a public one passes.
test('IPv6 forms that embed an IPv4 are checked as that IPv4 (compatible, NAT64, 6to4)', () => {
  for (const host of ['[::127.0.0.1]', '[::7f00:1]', '[::10.0.0.1]', '[::a9fe:a9fe]',
    '[64:ff9b::10.0.0.1]', '[64:ff9b::7f00:1]', '[64:ff9b::a9fe:a9fe]', '[64:ff9b::c0a8:101]',
    '[2002:7f00:1::]', '[2002:a00:1::1]', '[2002:a9fe:a9fe::]', '[2002:c0a8:101:5::9]', '[2002::]']) {
    assert.equal(isAllowedImageUrl(`http://${host}/x.png`), false, host);
  }
  for (const host of ['[64:ff9b::808:808]', '[64:ff9b::8.8.8.8]', '[2002:808:808::1]']) {
    assert.equal(isAllowedImageUrl(`http://${host}/x.png`), true, host);
  }
});

test('the local-use NAT64 prefix 64:ff9b:1::/48 is refused whatever it carries', () => {
  assert.equal(isAllowedImageUrl('http://[64:ff9b:1::808:808]/x.png'), false);
  assert.equal(isAllowedImageUrl('http://[64:ff9b:1:abcd::1]/x.png'), false);
});

test('allowLoopback admits an embedded loopback, never an embedded private or metadata IPv4', () => {
  const allow = { allowLoopback: true };
  assert.equal(isAllowedImageUrl('http://[::127.0.0.1]/x.png', allow), true);
  assert.equal(isAllowedImageUrl('http://[64:ff9b::10.0.0.1]/x.png', allow), false);
  assert.equal(isAllowedImageUrl('http://[2002:a9fe:a9fe::]/x.png', allow), false);
});

test('allowSameHostAs never unlocks the metadata IP through an IPv4-embedding IPv6', () => {
  for (const host of ['[::a9fe:a9fe]', '[64:ff9b::a9fe:a9fe]', '[2002:a9fe:a9fe::1]']) {
    assert.equal(isAllowedImageUrl(`http://${host}/x`, { allowSameHostAs: `http://${host}/` }), false, host);
  }
});
