// moveCheck's units: every top-level function, arrow-const and class method with the hash of
// its normalized body, and the multiset diff of two such lists.

import { createHash } from 'node:crypto';
import { WORD, mask, normalize } from './scan.mjs';

// ── unit extraction ─────────────────────────────────────────────
const DECL_CLASS = /^(?:export\s+)?(?:default\s+)?class\s+([\w$]+)/;
const DECL_FN = /^(?:export\s+)?(?:default\s+)?(?:async\s+)?function\s*\*?\s*([\w$]*)\s*\(/;
const DECL_MEMBER = /^(?:static\s+)?(?:async\s+)?(?:get\s+|set\s+)?\*?\s*([#\w$]+)\s*\(/;
const DECL_BIND = /^(?:export\s+)?(?:const|let|var|static)?\s*([#\w$]+)\s*=\s*(?!=)/;

const skipWs = (m, i) => { let j = i; while (j < m.length && /\s/.test(m[j])) j++; return j; };

const matchPair = (m, i, open, close) => {            // index just past the match for m[i]
  let depth = 0;
  for (let j = i; j < m.length; j++) {
    if (m[j] === open) depth++;
    else if (m[j] === close && --depth === 0) return j + 1;
  }
  return m.length;
};

// A `function …(…)` / `(…) =>` / `x =>` at p, and where its body starts.
const fnAt = (m, p) => {
  let j = skipWs(m, p);
  if (/^async[\s(]/.test(m.slice(j, j + 6))) j = skipWs(m, j + 5);
  const fm = /^function\s*\*?\s*[\w$]*\s*\(/.exec(m.slice(j, j + 200));
  if (fm) {
    const b = skipWs(m, matchPair(m, j + fm[0].length - 1, '(', ')'));
    return m[b] === '{' ? { start: b, block: true } : null;
  }
  let after;
  if (m[j] === '(') after = skipWs(m, matchPair(m, j, '(', ')'));
  else {
    const im = /^[\w$]+/.exec(m.slice(j, j + 200));
    if (!im) return null;
    after = skipWs(m, j + im[0].length);
  }
  if (m.slice(after, after + 2) !== '=>') return null;
  const b = skipWs(m, after + 2);
  return { start: b, block: m[b] === '{' };
};

// An expression-bodied arrow ends at the `;` (or the line break) that closes its statement.
const endOfExpr = (m, p) => {
  let j = p, depth = 0;
  while (j < m.length) {
    const c = m[j];
    if ('([{'.includes(c)) depth++;
    else if (')]}'.includes(c)) { if (depth === 0) return j; depth--; }
    else if (c === ';' && depth === 0) return j;
    else if (c === '\n' && depth === 0 && !/^[.?:+\-*/%&|,)\]}=<>]/.test(m[skipWs(m, j)] || '')) return j;
    j++;
  }
  return m.length;
};

const hashOf = (text) => createHash('sha256').update(text).digest('hex').slice(0, 12);
const lineAt = (src, i) => src.slice(0, i).split('\n').length;

// Every top-level function, arrow-const and class method, as { file, name, line, hash }.
export const unitsOf = (file, src) => {
  const m = mask(src, 'js');
  const units = [];
  const n = m.length;
  let i = 0, depth = 0, classBody = -1, className = '';
  const add = (name, start, end) => units.push({
    file, name: className ? `${className}.${name}` : name,
    line: lineAt(src, start), hash: hashOf(normalize(src.slice(start, end), 'js')),
  });
  while (i < n) {
    const c = m[i];
    if (c === '{') { depth++; i++; continue; }
    if (c === '}') { depth--; if (classBody >= 0 && depth < classBody) { classBody = -1; className = ''; } i++; continue; }
    const here = depth === 0 || depth === classBody;
    if (!here || !/[\w$#]/.test(c) || (i > 0 && WORD.test(m[i - 1]))) { i++; continue; }
    const rest = m.slice(i, i + 400);

    const cm = DECL_CLASS.exec(rest);
    if (cm) {
      const b = m.indexOf('{', i + cm[0].length);
      if (b >= 0) { className = cm[1]; depth++; classBody = depth; i = b + 1; continue; }
    }
    const fm = DECL_FN.exec(rest);
    if (fm) {
      const b = skipWs(m, matchPair(m, i + fm[0].length - 1, '(', ')'));
      if (m[b] === '{') { const e = matchPair(m, b, '{', '}'); add(fm[1] || '(anonymous)', b, e); i = e; continue; }
    }
    const mm = depth === classBody ? DECL_MEMBER.exec(rest) : null;
    if (mm && !/^(?:if|for|while|switch|catch|return)$/.test(mm[1])) {
      const b = skipWs(m, matchPair(m, i + mm[0].length - 1, '(', ')'));
      if (m[b] === '{') { const e = matchPair(m, b, '{', '}'); add(mm[1], b, e); i = e; continue; }
    }
    const bm = DECL_BIND.exec(rest);
    if (bm) {
      const fn = fnAt(m, i + bm[0].length);
      if (fn) {
        const e = fn.block ? matchPair(m, fn.start, '{', '}') : endOfExpr(m, fn.start);
        add(bm[1], fn.start, e);
        i = e;
        continue;
      }
    }
    i++;
  }
  return units;
};

// ── the diff ────────────────────────────────────────────────────
const bucket = (list) => {
  const by = new Map();
  for (const u of list) by.set(u.hash, [...(by.get(u.hash) || []), u]);
  return by;
};

export const diffUnits = (refUnits, headUnits) => {
  const a = bucket(refUnits), b = bucket(headUnits);
  const surplus = (from, other) => [...from].flatMap(([h, us]) => us.slice((other.get(h) || []).length));
  const unchanged = [...a].reduce((s, [h, us]) => s + Math.min(us.length, (b.get(h) || []).length), 0);
  return { lost: surplus(a, b), gained: surplus(b, a), unchanged };
};
