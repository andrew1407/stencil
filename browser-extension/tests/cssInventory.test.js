// Pins every CSS declaration the extension's documents load, so the stylesheets can be split,
// merged or reordered with proof that not one declaration changed. Twin of
// browser/tests/cssInventory.test.js over this surface's entry points: every src/**/*.html
// document (plus any manifest content_scripts CSS), each one's rules merged into one map
// keyed by "<at-rule prelude>|<selector>", a repeat merging last-wins as the cascade would
// at equal specificity. Moving a rule between files is invisible here, an edited value is
// not, and source order is pinned per document so a real reorder still fails.
// Re-pin an intended change: UPDATE_CSS_PIN=1 node --test tests/cssInventory.test.js

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, relative, resolve } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '..');
const PIN = resolve(HERE, 'pins/css.json');

// ── CSS scanner (builtins only) ────────────────────────────────
// Length of the atomic token starting at i, or -1 when it isn't one. Comments, strings,
// unquoted url() — which may hold a `;`, as data: URIs do — and `\` escapes are consumed
// whole, so they never look like syntax.
function atomLen(css, i) {
  const c = css[i];
  if (c === '\\') return 2;
  if (c === '/' && css[i + 1] === '*') {
    const end = css.indexOf('*/', i + 2);
    return (end < 0 ? css.length : end + 2) - i;
  }
  if (c === '"' || c === "'") {                         // a string runs to its unescaped close
    for (let j = i + 1; j < css.length; j++) {
      if (css[j] === '\\') j++;
      else if (css[j] === c) return j + 1 - i;
    }
    return css.length - i;
  }
  if (css.slice(i, i + 4).toLowerCase() === 'url(') {   // unquoted url() runs to its ')'
    let j = i + 4;
    while (j < css.length && css[j] !== ')') j += Math.max(atomLen(css, j), 1);
    return Math.min(j + 1, css.length) - i;
  }
  return -1;
}

// Split a block body into nodes: { prelude, start, end } for `… { … }`, { statement } for `…;`.
function parseNodes(css, from, to) {
  const nodes = [];
  let buf = '';
  for (let i = from; i < to;) {
    const a = atomLen(css, i);
    if (a > 0) {                                  // a comment reads as one space
      buf += css[i] === '/' && css[i + 1] === '*' ? ' ' : css.slice(i, i + a);
      i += a;
    } else if (css[i] === '{') {
      let depth = 0;
      let j = i;
      for (; j < to; j++) {
        const b = atomLen(css, j);
        if (b > 0) { j += b - 1; continue; }
        if (css[j] === '{') depth++;
        else if (css[j] === '}' && --depth === 0) break;
      }
      nodes.push({ prelude: buf.trim(), start: i + 1, end: Math.min(j, to) });
      buf = '';
      i = Math.min(j, to) + 1;
    } else if (css[i] === ';') {
      if (buf.trim()) nodes.push({ statement: buf.trim() });
      buf = '';
      i++;
    } else {
      buf += css[i++];
    }
  }
  if (buf.trim()) nodes.push({ statement: buf.trim() });
  return nodes;
}

const collapse = (s) => s.replace(/\s+/g, ' ').trim();

// Split on commas outside parens/brackets/strings, so `:is(a, b)` stays one part.
function topLevelSplit(sel) {
  const parts = [''];
  let depth = 0;
  for (let i = 0; i < sel.length;) {
    const a = atomLen(sel, i);
    if (a > 0) { parts[parts.length - 1] += sel.slice(i, i + a); i += a; continue; }
    const c = sel[i++];
    if (c === '(' || c === '[') depth++;
    else if (c === ')' || c === ']') depth--;
    if (c === ',' && depth === 0) parts.push('');
    else parts[parts.length - 1] += c;
  }
  return parts;
}

// A selector list is order-free at equal specificity, so sort it into one canonical form.
const normalizeSelector = (sel) => topLevelSplit(sel).map(collapse).filter(Boolean).sort().join(', ');
// @media/@keyframes/… hold rules; @font-face/@property hold declarations.
const holdsRules = (nodes) => nodes.some((n) => n.prelude !== undefined);

function walk(css, from, to, context, out) {
  for (const node of parseNodes(css, from, to)) {
    if (node.statement !== undefined) {
      if (node.statement.startsWith('@')) out.add(context, collapse(node.statement), {});
      continue;
    }
    const inner = parseNodes(css, node.start, node.end);
    const isAt = node.prelude.startsWith('@');
    if (isAt && holdsRules(inner)) {
      walk(css, node.start, node.end, [context, collapse(node.prelude)].filter(Boolean).join(' '), out);
      continue;
    }
    const decls = {};
    for (const { statement = '' } of inner) {
      const at = statement.indexOf(':');
      if (at < 0) continue;
      const prop = collapse(statement.slice(0, at));   // custom properties keep their case
      decls[prop.startsWith('--') ? prop : prop.toLowerCase()] = collapse(statement.slice(at + 1));
    }
    out.add(context, isAt ? collapse(node.prelude) : normalizeSelector(node.prelude), decls);
  }
}

function newInventory() {
  const rules = new Map();
  const order = [];
  return {
    add(context, selector, decls) {
      const key = `${context}|${selector}`;
      order.push(key);
      if (!rules.has(key)) rules.set(key, {});
      Object.assign(rules.get(key), decls);   // last-wins, as the cascade resolves a repeat
    },
    // Sorted keys keep the pin file diff-friendly; `order` stays in source order.
    finish: () => ({ rules: Object.fromEntries([...rules.keys()].sort().map((k) => [k, rules.get(k)])), order }),
  };
}

// Every <link rel=stylesheet> href and inline <style> of one document, in document order.
function sheetsOf(htmlPath) {
  const sheets = [];
  for (const m of readFileSync(htmlPath, 'utf8').matchAll(/<link\b[^>]*>|<style\b[^>]*>([\s\S]*?)<\/style>/gi)) {
    if (/^<style/i.test(m[0])) { sheets.push({ name: `${relative(ROOT, htmlPath)} <style>`, css: m[1] }); continue; }
    if (!/rel\s*=\s*["']?\s*stylesheet\b/i.test(m[0])) continue;
    const href = m[0].match(/href\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i);
    const url = href && (href[1] ?? href[2] ?? href[3]);
    if (!url || /^(https?:)?\/\/|^data:/i.test(url)) continue;   // only sheets that live in the tree
    const file = resolve(dirname(htmlPath), url);
    sheets.push({ name: relative(ROOT, file), css: readFileSync(file, 'utf8') });
  }
  return sheets;
}

function entryPoints() {   // every document the extension ships, plus injected content-script CSS
  const docs = readdirSync(resolve(ROOT, 'src'), { recursive: true }).filter((f) => f.endsWith('.html'))
    .map((f) => `src/${f}`.replaceAll('\\', '/')).sort()
    .map((entry) => ({ entry, sheets: sheetsOf(resolve(ROOT, entry)) }));
  const { content_scripts: scripts = [] } = JSON.parse(readFileSync(resolve(ROOT, 'manifest.json'), 'utf8'));
  scripts.forEach((cs, i) => docs.push({ entry: `content_scripts[${i}]`,
    sheets: (cs.css ?? []).map((f) => ({ name: f, css: readFileSync(resolve(ROOT, f), 'utf8') })) }));
  return docs.filter((d) => d.sheets.length);   // a document with no stylesheet pins nothing
}
const current = {};
for (const { entry, sheets } of entryPoints()) {
  const out = newInventory();
  for (const sheet of sheets) walk(sheet.css, 0, sheet.css.length, '', out);
  current[entry] = { sheets: sheets.map((s) => s.name), ...out.finish() };
}
const ENTRIES = Object.keys(current);
const pinnable = Object.fromEntries(Object.entries(current).map(([e, i]) => [e, { rules: i.rules, order: i.order }]));
if (process.env.UPDATE_CSS_PIN === '1') writeFileSync(PIN, `${JSON.stringify(pinnable, null, 1)}\n`);
const pin = JSON.parse(readFileSync(PIN, 'utf8'));

for (const entry of ENTRIES) {
  test(`css inventory: ${entry} declarations match the pin`, () => {
    const have = current[entry].rules;
    const want = pin[entry]?.rules ?? {};
    assert.ok(current[entry].sheets.length && Object.keys(have).length > 5, 'the scanner or the hrefs broke');
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
  assert.deepEqual(ENTRIES, Object.keys(pin), 'an entry point gained or lost its stylesheets — re-pin if intended');
});

// ── the scanner itself ───────────────────────────────────────
const scan = (css) => { const out = newInventory(); walk(css, 0, css.length, '', out); return out.finish(); };

test('css scanner: comments, strings and url() never look like syntax', () => {
  const { rules } = scan(`/* a { color: red } */
    .a::after { content: "}"; background: url(data:image/svg+xml;base64,AA/*x*/BB) no-repeat; }
    .b { content: '\\;'; color: red }`);
  assert.deepEqual(Object.keys(rules).sort(), ['|.a::after', '|.b']);
  assert.equal(rules['|.a::after'].background, 'url(data:image/svg+xml;base64,AA/*x*/BB) no-repeat');
  assert.equal(rules['|.b'].content, "'\\;'");
});

test('css scanner: selector lists sort, whitespace collapses, repeats merge last-wins', () => {
  const { rules, order } = scan('.b ,\n.a { color: red; color: blue }  .a,.b { margin:0 }');
  assert.deepEqual(rules, { '|.a, .b': { color: 'blue', margin: '0' } });
  assert.deepEqual(order, ['|.a, .b', '|.a, .b']);
});

test('css scanner: at-rules key by prelude and nest', () => {
  const { rules } = scan(`@media (min-width: 40rem) { @supports (display: grid) { .a { display: grid } } }
    @keyframes spin { from { rotate: 0deg } to { rotate: 360deg } }
    @property --x { syntax: '<length>'; inherits: false }`);
  assert.deepEqual(Object.keys(rules).sort(), ['@keyframes spin|from', '@keyframes spin|to',
    '@media (min-width: 40rem) @supports (display: grid)|.a', '|@property --x']);
  assert.equal(rules['|@property --x'].syntax, "'<length>'");
});
