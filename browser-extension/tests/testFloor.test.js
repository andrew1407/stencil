// Test-count floor for browser-extension/: tools/repo/testRun.mjs reads `stencil.testFloor` from package.json
// after a bare `npm test` and fails "collapsed to N, floor is M", since green says nothing about how
// many tests ran. This only holds the key and the script that honours it in place.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

const manifest = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));

test('test-count floor: package.json names one, and npm test runs the runner that holds it', () => {
  const floor = manifest.stencil?.testFloor;
  assert.ok(Number.isInteger(floor) && floor > 0, `stencil.testFloor must be a positive integer, got ${floor}`);
  assert.equal(manifest.scripts.test, 'node ../tools/repo/testRun.mjs', 'a bare npm test must go through the floor-holding runner');
});
