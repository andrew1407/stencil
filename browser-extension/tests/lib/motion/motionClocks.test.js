// The motion clocks lib/motion carries as literals (lib/ may not import config/) and that the shared table also
// holds: each is pinned to the extension's drift-tested copy of common/config/motion.json, so a retune there fails
// here until the literal follows. Browser twin: the same keys read live through ui/motion/tune.js.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');
const { ui } = JSON.parse(read('../../../src/config/motion.json'));
const literal = (src, name) => {
  const found = src.match(new RegExp(`const ${name} = (\\d+);`));
  assert.ok(found, `${name} is a literal in the module`);
  return Number(found[1]);
};

test('the tip clocks match the shared motion table', () => {
  const src = read('../../../src/lib/motion/tips.js');
  for (const key of ['TIP_GROW_MS', 'TIP_SHRINK_MS', 'TIP_SLIDE_IN_MS', 'TIP_SLIDE_OUT_MS']) assert.equal(literal(src, key), ui[key], key);
});

test('the chat clocks match the shared motion table', () => {
  const src = read('../../../src/lib/motion/chatFx.js');
  assert.equal(literal(src, 'CHAT_SIDE_MS'), ui.CHAT_SIDE_MS);
  assert.equal(literal(src, 'CHAT_SLIDE_MS'), ui.CHAT_SLIDE_MS);
});
