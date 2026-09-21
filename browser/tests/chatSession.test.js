// The §10 project-name helpers of js/llm/session.js: unique naming and name resolution.
// The capability closures and the logged-turn frame are in the sibling chatSession specs.
import { test } from 'node:test';
import assert from 'node:assert';

import { uniqueProjectName, resolveProjectByName } from '../js/llm/chat/session.js';

// ── §10 name helpers (pure) ─────────────────────────────────────────────────
test('uniqueProjectName suffixes until the name is free', () => {
  // No store (or one without nameExists) → the wanted name, unchanged.
  assert.strictEqual(uniqueProjectName({}, 'cat'), 'cat');
  assert.strictEqual(uniqueProjectName({ storage: { store: {} } }, 'cat'), 'cat');
  const app = (taken) => ({ storage: { store: { nameExists: (n) => taken.includes(n) } } });
  assert.strictEqual(uniqueProjectName(app([]), 'cat'), 'cat');
  // A batch of saves wanting the same base counts up instead of losing the save.
  assert.strictEqual(uniqueProjectName(app(['cat']), 'cat'), 'cat 2');
  assert.strictEqual(uniqueProjectName(app(['cat', 'cat 2']), 'cat'), 'cat 3');
});

test('resolveProjectByName: exact beats prefix, ambiguity and misses are notes', () => {
  const app = (names) => ({ storage: { store: { list: () => names.map((name, i) => ({ id: i, name })) } } });
  // Exact match wins even when it is also a prefix of another name.
  assert.deepStrictEqual(resolveProjectByName(app(['cat', 'cathedral']), 'cat').meta, { id: 0, name: 'cat' });
  // A unique case-insensitive prefix resolves…
  assert.deepStrictEqual(resolveProjectByName(app(['Cathedral', 'dog']), 'cat').meta, { id: 0, name: 'Cathedral' });
  // …but an ambiguous one asks for the full name instead of guessing.
  assert.strictEqual(resolveProjectByName(app(['cathedral', 'cattle']), 'cat').note,
    '"cat" matches 2 projects — use the full name');
  assert.strictEqual(resolveProjectByName(app(['dog']), 'cat').note, 'no saved project named "cat"');
});
