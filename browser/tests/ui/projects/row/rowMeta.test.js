// What a project row says about its expiry (js/ui/projects/row/meta.js): a project kept forever
// says so, rather than saying nothing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRowMeta } from '../../../../js/ui/projects/row/meta.js';
import * as periods from '../../../../js/core/project/meta/projectPeriods.js';

const DAY = 24 * 60 * 60 * 1000;
const store = {
  isExpired: (m) => periods.isExpired(m, Date.now()),
  expiresAt: (m) => periods.expiresAt(m),
  isExpiringSoon: (m) => periods.isExpiringSoon(m, Date.now()),
};
const { expiryLabel } = createRowMeta(store);

test('a project kept forever reads "never expires"', () => {
  assert.deepEqual(expiryLabel({ expiresAt: 0 }), { text: 'never expires', expired: false, soon: false });
  assert.equal(expiryLabel({}).text, 'never expires');
});

test('a dated project counts its days down, and a lapsed one is EXPIRED', () => {
  assert.equal(expiryLabel({ expiresAt: Date.now() + 10 * DAY - 1000 }).text, 'expires in 10 days');
  assert.equal(expiryLabel({ expiresAt: Date.now() + 1000 }).text, 'expires in 1 day');
  assert.equal(expiryLabel({ expiresAt: Date.now() - 1000 }).text, 'EXPIRED');
});

test('the row menu re-lists the projects once an expiration is saved', async () => {
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../../../../js/ui/projects/row/actions.js', import.meta.url), 'utf8');
  assert.match(src, /stencil-expiration-modal'\)\?\.openFor\(meta\.id, \{ from: at, backTo: menuBtn, onSaved: render \}\)/);
});
