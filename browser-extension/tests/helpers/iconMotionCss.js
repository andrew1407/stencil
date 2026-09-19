// The icon-motion section of both sheets, parsed: the canonical design table, the extension's and
// the app's rules, and the lookups the iconMotion*.test.js suites compare them with.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { animationsCss, browserAnimationsCss } from './sources.js';

import { ICONS } from '../../src/lib/icons.js';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');
export const MOTION = JSON.parse(read('../../../browser/js/config/iconMotion.json'));

// The icon-motion section of a sheet: its comment header up to the next section.
export const section = (css, from, to) => {
  const a = css.indexOf(from);
  const b = css.indexOf(to);
  assert.ok(a >= 0 && b > a, `the icon-motion section is where the tests expect it (${from})`);
  return css.slice(a, b);
};
export const EXT = section(animationsCss(),
  '/* ── Icon hover: every glyph mimes its own action', '/* ── Header logo hover');
export const APP = section(browserAnimationsCss(),
  '/* ── Icon hover: every glyph mimes its own action', '/* ── App logo hover');

// Glyphs lib/icons.js carries that the browser has no twin for (dataParity.test.js
// holds the same list): they have no canonical design and must stay still.
export const EXTENSION_ONLY = ['sidebar', 'type'];
export const DESIGNED = Object.keys(ICONS).filter((n) => !EXTENSION_ONLY.includes(n));

// Enough for this section: plain rules, @keyframes (a leaf — its body compared whole) and one
// @media block. Comments are stripped and whitespace collapsed, so indentation alone compares equal.
export const squash = (s) => s.replace(/\s+/g, ' ').trim();
export const parse = (css) => {
  const rules = [];        // { at, prelude, body }
  const walk = (text, at) => {
    let i = 0;
    while (i < text.length) {
      const open = text.indexOf('{', i);
      if (open < 0) break;
      const prelude = squash(text.slice(i, open));
      let depth = 1, j = open + 1;
      while (j < text.length && depth) {
        if (text[j] === '{') depth++;
        else if (text[j] === '}') depth--;
        j++;
      }
      const body = text.slice(open + 1, j - 1);
      if (/^@(media|supports)/.test(prelude)) walk(body, prelude);
      else rules.push({ at, prelude, body: squash(body) });
      i = j;
    }
  };
  walk(css.replace(/\/\*[\s\S]*?\*\//g, ''), '');
  return rules;
};
export const extRules = parse(EXT);
export const appRules = parse(APP);
// selector (one comma-separated part) → the bodies declared for it
export const bySelector = (rules) => {
  const map = new Map();
  for (const r of rules) {
    if (r.prelude.startsWith('@')) continue;
    for (const sel of r.prelude.split(',').map(squash))
      map.set(sel, [...(map.get(sel) || []), r.body]);
  }
  return map;
};
export const APP_SEL = bySelector(appRules);
export const keyframesOf = (rules) => new Map(rules
  .filter((r) => r.prelude.startsWith('@keyframes'))
  .map((r) => [r.prelude.replace('@keyframes', '').trim(), r.body]));

export const partsOf = (d) => [...d.parts, ...Object.values(d.variants || {}).flatMap((v) => v.parts)];
export const hooksOf = (d) => [...new Set(partsOf(d).map((p) => p.hook).filter(Boolean))];
