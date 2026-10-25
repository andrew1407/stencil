// Pins every CSS declaration the app loads, so the stylesheets can be split, merged or reordered with proof
// that not one declaration changed. FILE-BLIND: it follows index.html's <link rel=stylesheet> hrefs and its
// inline <style>, then merges every rule into one map keyed by "<at-rule prelude>|<selector>", a repeat
// merging last-wins as the cascade would at equal specificity; source order is pinned separately, so a real
// cascade reorder still fails. Re-pin: UPDATE_CSS_PIN=1 node --test tests/cssInventory.test.js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { inventory, sheetsOf } from '../../tools/repo/cssInventory.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '..');
const PIN = resolve(HERE, 'pins/css.json');
const ENTRIES = ['index.html'];

const current = {};
for (const entry of ENTRIES) {
  const sheets = sheetsOf(resolve(ROOT, entry), ROOT);
  current[entry] = { sheets: sheets.map((s) => s.name), ...inventory(sheets) };
}
const pinnable = Object.fromEntries(Object.entries(current).map(([e, i]) => [e, { rules: i.rules, order: i.order }]));
if (process.env.UPDATE_CSS_PIN === '1') writeFileSync(PIN, `${JSON.stringify(pinnable, null, 1)}\n`);
const pin = JSON.parse(readFileSync(PIN, 'utf8'));

for (const entry of ENTRIES) {
  test(`css inventory: ${entry} declarations match the pin`, () => {
    const have = current[entry].rules;
    const want = pin[entry]?.rules ?? {};
    assert.ok(current[entry].sheets.length && Object.keys(have).length > 100, 'the scanner or the hrefs broke');
    const added = Object.keys(have).filter((k) => !(k in want));
    const removed = Object.keys(want).filter((k) => !(k in have));
    const changed = Object.keys(have).filter((k) => k in want && JSON.stringify(have[k]) !== JSON.stringify(want[k]))
      .map((k) => `${k}\n      pin: ${JSON.stringify(want[k])}\n      now: ${JSON.stringify(have[k])}`);
    const report = [['added', added], ['removed', removed], ['changed', changed]].filter(([, l]) => l.length)
      .map(([label, l]) => `  ${label} (${l.length}):\n    ${l.join('\n    ')}`).join('\n');
    assert.ok(!report, `CSS changed for ${entry} — re-pin with UPDATE_CSS_PIN=1 if intended:\n${report}`);
    assert.deepEqual(have, want);
  });

  test(`css inventory: ${entry} cascade order matches the pin`, () => {
    const have = current[entry].order;
    const want = pin[entry]?.order ?? [];
    const at = have.findIndex((k, i) => k !== want[i]);
    assert.ok(at < 0 && have.length === want.length,
      `cascade order changed for ${entry} at index ${at}: pin ${JSON.stringify(want[at])} vs now ${JSON.stringify(have[at])}`);
  });
}


test('css inventory: the pinned documents are the ones that exist', () => {
  assert.deepEqual(Object.keys(current), Object.keys(pin), 'an entry point gained or lost its stylesheets — re-pin if intended');
});
