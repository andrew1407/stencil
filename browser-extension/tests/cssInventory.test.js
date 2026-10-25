// Pins every CSS declaration the extension's documents load, so the stylesheets can be split,
// merged or reordered with proof that not one declaration changed. Twin of
// browser/tests/cssInventory.test.js over this surface's entry points: every src/**/*.html document
// and the content-script CSS, each stylesheet's rules keyed by "<at-rule prelude>|<selector>".
// Re-pin an intended change: UPDATE_CSS_PIN=1 node --test tests/cssInventory.test.js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { inventory, sheetsOf } from '../../tools/repo/cssInventory.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '..');
const PIN = resolve(HERE, 'pins/css.json');

function entryPoints() {   // every document the extension ships, plus injected content-script CSS
  const docs = readdirSync(resolve(ROOT, 'src'), { recursive: true }).filter((f) => f.endsWith('.html'))
    .map((f) => `src/${f}`.replaceAll('\\', '/')).sort()
    .map((entry) => ({ entry, sheets: sheetsOf(resolve(ROOT, entry), ROOT) }));
  const { content_scripts: scripts = [] } = JSON.parse(readFileSync(resolve(ROOT, 'manifest.json'), 'utf8'));
  scripts.forEach((cs, i) => docs.push({ entry: `content_scripts[${i}]`,
    sheets: (cs.css ?? []).map((f) => ({ name: f, css: readFileSync(resolve(ROOT, f), 'utf8') })) }));
  return docs.filter((d) => d.sheets.length);   // a document with no stylesheet pins nothing
}
// Pinned per stylesheet (a sheet several documents load is pinned once) plus each document's ordered
// sheet list: a document's cascade is its sheets' rules in that order, so the two together prove it.
const sheets = {};
const documents = {};
for (const { entry, sheets: list } of entryPoints()) {
  documents[entry] = list.map((s) => s.name);
  for (const sheet of list) sheets[sheet.name] ??= inventory([sheet]);
}
const SHEETS = Object.keys(sheets).sort();
if (process.env.UPDATE_CSS_PIN === '1') {
  const pinnable = { documents, sheets: Object.fromEntries(SHEETS.map((n) => [n, sheets[n]])) };
  writeFileSync(PIN, `${JSON.stringify(pinnable, null, 1)}\n`);
}
const pin = JSON.parse(readFileSync(PIN, 'utf8'));

for (const name of SHEETS) {
  test(`css inventory: ${name} declarations match the pin`, () => {
    const have = sheets[name].rules;
    const want = pin.sheets?.[name]?.rules ?? {};
    const added = Object.keys(have).filter((k) => !(k in want));
    const removed = Object.keys(want).filter((k) => !(k in have));
    const changed = Object.keys(have).filter((k) => k in want && JSON.stringify(have[k]) !== JSON.stringify(want[k]))
      .map((k) => `${k}\n      pin: ${JSON.stringify(want[k])}\n      now: ${JSON.stringify(have[k])}`);
    const report = [['added', added], ['removed', removed], ['changed', changed]].filter(([, l]) => l.length)
      .map(([label, l]) => `  ${label} (${l.length}):\n    ${l.join('\n    ')}`).join('\n');
    assert.ok(!report, `CSS changed in ${name} — re-pin with UPDATE_CSS_PIN=1 if intended:\n${report}`);
    assert.deepEqual(have, want);
  });

  test(`css inventory: ${name} cascade order matches the pin`, () => {
    const have = sheets[name].order;
    const want = pin.sheets?.[name]?.order ?? [];
    const at = have.findIndex((k, i) => k !== want[i]);
    assert.ok(at < 0 && have.length === want.length,
      `cascade order changed in ${name} at index ${at}: pin ${JSON.stringify(want[at])} vs now ${JSON.stringify(have[at])}`);
  });
}

test('css inventory: every document loads the pinned sheets, in the pinned order', () => {
  assert.deepEqual(documents, pin.documents, 'a document gained, lost or reordered a stylesheet — re-pin if intended');
  assert.deepEqual(SHEETS, Object.keys(pin.sheets ?? {}), 'a stylesheet appeared or went — re-pin if intended');
  assert.ok(Object.keys(documents).length > 3 && Object.values(sheets).reduce((n, s) => n + Object.keys(s.rules).length, 0) > 100,
    'the scanner or the hrefs broke');
});
