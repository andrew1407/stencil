// The .d.ts rail's second half: a function both a module and its shape file declare takes the
// same parameters. A declaration may add optional ones past the module's, never fall short of
// them or require one the module never reads. The mismatches already in the tree are frozen
// below and only shrink: a fixed one leaves the list, a new one fails.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const JS = fileURLToPath(new URL('../js', import.meta.url));
const read = (p) => readFileSync(p, 'utf8');
const walk = (dir, out = []) => {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) { if (e.name !== 'wasm') walk(p, out); } else if (e.name.endsWith('.d.ts')) out.push(p);
  }
  return out;
};
const stripComments = (s) => s.replace(/\/\*[\s\S]*?\*\//g, '').replace(/(^|[^:'"`\\])\/\/.*$/gm, '$1');

// The text inside the parenthesis that opens at `at`, and the index after its close.
const group = (src, at) => {
  let depth = 0;
  for (let i = at; i < src.length; i++) {
    const c = src[i];
    if (c === '"' || c === "'" || c === '`') { const q = c; for (i++; i < src.length && src[i] !== q; i++) if (src[i] === '\\') i++; continue; }
    if ('([{<'.includes(c) && !(c === '<' && src[i + 1] === '=')) depth++;
    else if (')]}>'.includes(c) && !(c === '>' && src[i - 1] === '=')) { depth--; if (depth === 0) return src.slice(at + 1, i); }
  }
  return null;
};
// Top-level parameters: { total, required, rest }.
const params = (inner) => {
  const parts = [];
  let depth = 0, cur = '';
  for (let i = 0; i < inner.length; i++) {
    const c = inner[i];
    if ('([{<'.includes(c) && !(c === '<' && inner[i + 1] === '=')) depth++;
    else if (')]}>'.includes(c) && !(c === '>' && inner[i - 1] === '=')) depth--;
    if (c === ',' && depth === 0) { parts.push(cur); cur = ''; } else cur += c;
  }
  parts.push(cur);
  const list = parts.map((p) => p.trim()).filter(Boolean);
  const rest = list.some((p) => p.startsWith('...'));
  const required = list.filter((p) => !p.startsWith('...') && !/^[\w$]+\?\s*:/.test(p) && !/^[^:]*=/.test(p.replace(/=>/g, ''))).length;
  return { total: list.length - (rest ? 1 : 0), required, rest };
};

// name → parameters of each exported function a module defines.
const moduleArities = (src) => {
  const out = new Map();
  const code = stripComments(src);
  for (const m of code.matchAll(/^export (?:async )?function\*?\s+(\w+)\s*\(/gm)) out.set(m[1], params(group(code, m.index + m[0].length - 1)));
  for (const m of code.matchAll(/^export const (\w+)\s*=\s*(?:async\s+)?(?:function\s*\w*\s*)?(\(|[\w$]+\s*=>)/gm)) {
    out.set(m[1], m[2] === '(' ? params(group(code, m.index + m[0].length - 1)) : { total: 1, required: 1, rest: false });
  }
  return out;
};
// name → the declared parameters (the widest of any overloads).
const declaredArities = (src) => {
  const out = new Map();
  const code = stripComments(src);
  const put = (name, inner) => {
    if (inner == null) return;
    const p = params(inner), was = out.get(name);
    out.set(name, was ? { total: Math.max(was.total, p.total), required: Math.min(was.required, p.required), rest: was.rest || p.rest } : p);
  };
  for (const m of code.matchAll(/^export (?:declare )?function\s+(\w+)\s*(<)?/gm)) {
    let at = m.index + m[0].length - (m[2] ? 1 : 0);
    if (m[2]) at += group(code, at).length + 2;
    put(m[1], group(code, code.indexOf('(', at)));
  }
  for (const m of code.matchAll(/^export (?:declare )?const (\w+)\s*:\s*(<[^(]*>\s*)?\(/gm)) put(m[1], group(code, m.index + m[0].length - 1));
  return out;
};

const mismatches = () => {
  const found = [];
  let compared = 0;
  for (const dts of walk(JS).sort()) {
    const js = dts.replace(/\.d\.ts$/, '.js');
    if (!existsSync(js)) continue;
    const mod = moduleArities(read(js)), decl = declaredArities(read(dts));
    for (const [name, m] of mod) {
      const d = decl.get(name);
      if (!d || m.rest || d.rest) continue;
      compared++;
      if (d.total < m.total || d.required > m.total) found.push(`${path.relative(JS, js)} ${name} ${d.total}/${m.total}`);
    }
  }
  return { found, compared };
};

// `module name declared/actual` — each one a shape file that disagrees with its module today.
const FROZEN = [];

test('every declared function takes the parameters its module does, beyond the frozen list', () => {
  const { found: now, compared } = mismatches();
  assert.ok(compared > 1100, `only ${compared} declared functions were compared — the counter stopped reading the tree`);
  assert.deepEqual(now.filter((m) => !FROZEN.includes(m)), [], 'a new arity mismatch: fix the .d.ts');
  assert.deepEqual(FROZEN.filter((m) => !now.includes(m)), [], 'fixed — drop it from FROZEN');
});

test('the counter reads the forms the tree uses', () => {
  assert.deepEqual(params('a, { b, c } = {}, d?: number'), { total: 3, required: 1, rest: false });
  assert.deepEqual(params('fn: (x: number, y: number) => void, opts?: Record<string, number>'), { total: 2, required: 1, rest: false });
  assert.equal(moduleArities('export const f = async (a, b = 1) => a;').get('f').total, 2);
  assert.equal(moduleArities('export const g = x => x;').get('g').total, 1);
  assert.equal(declaredArities('export declare function h<T extends (a: number) => void>(t: T, n?: number): void;').get('h').total, 2);
  assert.equal(declaredArities('export declare const k: <T>(a: T, b: T) => T;').get('k').total, 2);
});
