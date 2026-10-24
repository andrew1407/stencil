// The launch-query corpus (common/fixtures/deepLink/launchQuery.json, format in its _schema.md):
// every vector's URL parts parse to its expectation, the fields it leaves out being EMPTY's.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseLaunchQuery, emptyLaunchQuery } from '../../../js/core/launch/query.js';

const VECTORS = JSON.parse(readFileSync(
  new URL('../../../../common/fixtures/deepLink/launchQuery.json', import.meta.url), 'utf8'));

test('launchQuery.json: every vector parses to its expectation', () => {
  assert.ok(VECTORS.length >= 20, 'the corpus is in place');
  for (const v of VECTORS) {
    const got = parseLaunchQuery(v.search ?? '', v.hash ?? '');
    const want = v.expect === null ? null : { ...emptyLaunchQuery(), ...v.expect };
    assert.deepEqual(got, want, v.name);
  }
});

test('launchQuery.json: vector names are unique and each names a URL part', () => {
  const names = VECTORS.map((v) => v.name);
  assert.equal(new Set(names).size, names.length, 'duplicate vector name');
  for (const v of VECTORS) {
    assert.ok(typeof v.name === 'string' && v.name, 'every vector is named');
    assert.ok('search' in v || 'hash' in v, `${v.name} carries a search or a hash`);
    assert.ok('expect' in v, `${v.name} states its expectation`);
  }
});
