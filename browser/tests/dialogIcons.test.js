import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';

// Dialog glyphs (js/ui/confirmModal.js). The modal falls back to a generic check mark, so a button that says
// what it DOES names its own icon via confirmIcon / altIcon. Two silent failures are caught: a typo'd or
// renamed glyph name, which renders nothing at all, and a destructive dialog left on the default tick, where
// a check mark beside "This cannot be undone" reads as reassurance.
import { ICONS } from '../js/ui/icons.js';

const JS_ROOT = new URL('../js/', import.meta.url).pathname;

const sourceFiles = (dir) => {
  const out = [];
  for (const entry of readdirSync(dir)) {
    const p = join(dir, entry);
    if (statSync(p).isDirectory()) out.push(...sourceFiles(p));
    else if (entry.endsWith('.js')) out.push(p);
  }
  return out;
};

const FILES = sourceFiles(JS_ROOT).map((p) => [p.slice(JS_ROOT.length), readFileSync(p, 'utf8')]);

test('every confirmIcon / altIcon names a glyph that exists', () => {
  const seen = [];
  for (const [rel, src] of FILES) {
    for (const m of src.matchAll(/(?:confirmIcon|altIcon):\s*'([^']*)'/g)) {
      seen.push([rel, m[1]]);
      assert.ok(ICONS[m[1]], `${rel}: "${m[1]}" is not in the icon set`);
    }
  }
  assert.ok(seen.length >= 20, `expected the dialogs to be labelled; found ${seen.length}`);
});

test('destructive dialogs do not confirm with a check mark', () => {
  // `danger: true` marks a confirm the user cannot undo. Its options object must carry an
  // icon of its own — trash for a delete, crop for one that discards placed lines, and so on.
  const offenders = [];
  for (const [rel, src] of FILES) {
    if (rel === 'ui/confirmModal.js') continue;   // the component, not a call site
    // Each `danger: true` sits inside one options object: scan from the nearest "{" that
    // opens it to the matching "}" by walking back to the start of the literal.
    for (const m of src.matchAll(/danger:\s*true/g)) {
      const open = src.lastIndexOf('{', m.index);
      const close = src.indexOf('}', m.index);
      const opts = src.slice(open, close + 1);
      // `danger` also marks destructive MENU items ({ icon, label, danger, onClick }),
      // which carry their glyph under `icon`. Dialog options are the ones with a title.
      if (!opts.includes('title')) continue;
      if (!opts.includes('confirmIcon')) {
        const line = src.slice(0, m.index).split('\n').length;
        offenders.push(`${rel}:${line}`);
      }
    }
  }
  assert.deepStrictEqual(offenders, [], 'destructive confirms missing an explicit glyph');
});
