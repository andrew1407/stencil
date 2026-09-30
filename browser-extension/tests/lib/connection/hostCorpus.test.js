// Walks common/fixtures/net/hosts.json through the real guard: each host reaches
// isAllowedImageUrl inside a URL, as a page would hand it over, and the table judge answers the
// serverTarget variants the extension itself never dials.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { isAllowedImageUrl } from '../../../src/lib/connection/urlGuard.js';
import { isBlockedAddress, parseAddress } from '../../../src/lib/connection/addressRanges.js';

const CASES = JSON.parse(readFileSync(
  new URL('../../../../common/fixtures/net/hosts.json', import.meta.url), 'utf8'));

const urlHost = (host) => {
  const bare = host.replace(/^\[|\]$/g, '');
  return (bare.includes(':') ? `[${bare}]` : bare).replace('%', '%25');
};
const hostnameOf = (url) => { try { return new URL(url).hostname; } catch { return null; } };
const verdict = (blocked) => (blocked ? 'block' : 'allow');

for (const c of CASES) {
  test(`host corpus: ${c.name} (${c.host})`, () => {
    const url = `http://${urlHost(c.host)}/x.png`;
    const hostname = hostnameOf(url);
    if (!c.expect) {
      assert.equal(parseAddress(hostname), null, 'a name is never read as an address');
      assert.equal(isAllowedImageUrl(url), true);
      return;
    }
    // A WHATWG URL carries no zone ID, so the URL itself is refused before any host check.
    if (hostname !== null) assert.deepEqual(parseAddress(hostname), parseAddress(c.address));
    else assert.ok(c.host.includes('%'), 'only a zone ID may make the URL unparseable');
    assert.equal(verdict(!isAllowedImageUrl(url)), c.expect.fetch, 'fetch');
    assert.equal(verdict(!isAllowedImageUrl(url, { allowLoopback: true })), c.expect['fetch+allowLoopback'],
      'fetch+allowLoopback');
    const bytes = parseAddress(c.address);
    assert.equal(verdict(isBlockedAddress(bytes, 'serverTarget')), c.expect.serverTarget, 'serverTarget');
    assert.equal(verdict(isBlockedAddress(bytes, 'serverTarget', { allowPrivate: true })),
      c.expect['serverTarget+allowPrivate'], 'serverTarget+allowPrivate');
  });
}
