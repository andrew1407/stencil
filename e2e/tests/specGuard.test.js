// The two guards on the spec tree, run by `npm test` before Playwright: a test-count floor
// (Playwright can only be asked what it runs by running it, so this counts `test(`
// declarations — it catches a deleted spec, not an unexecuted one) and the runner's own
// blind spot — every project sets its own testMatch, so a spec no project names is collected
// by nothing and reports neither pass nor skip.
import test from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import config from '../playwright.config.js';

// A floor, not a pin: raise it when the suite grows a lot; additions must never trip it.
const TEST_FLOOR = 260;
const DECL = /^\s*test(?:\.\w+)*\s*\(/gm;
const E2E = fileURLToPath(new URL('../', import.meta.url));
const SKIP_DIRS = new Set(['node_modules', 'test-results', 'playwright-report', 'blob-report', '.playwright']);

const specs = (dir = '', out = []) => {
  for (const e of fs.readdirSync(path.join(E2E, dir), { withFileTypes: true })) {
    const rel = dir ? `${dir}/${e.name}` : e.name;
    if (e.isDirectory()) { if (!SKIP_DIRS.has(e.name)) specs(rel, out); }
    else if (e.name.endsWith('.spec.js')) out.push(rel);
  }
  return out.sort();
};

test('test-count floor: the spec tree still declares its cases', () => {
  let count = 0;
  for (const rel of specs()) count += (fs.readFileSync(path.join(E2E, rel), 'utf8').match(DECL) || []).length;
  assert.ok(count >= TEST_FLOOR, `e2e suite collapsed to ${count} declared tests, floor is ${TEST_FLOOR}`);
});

test('every spec is claimed by a playwright project', () => {
  const orphans = specs().filter((rel) => !config.projects.some((p) => p.testMatch.test(rel)));
  assert.deepStrictEqual(orphans, [], 'add a project testMatch in playwright.config.js, or move the spec');
});
