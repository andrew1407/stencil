// The CSS inventory scanner both surfaces pin their stylesheets with (browser/tests/cssInventory.test.js,
// browser-extension/tests/cssInventory.test.js): every rule of a document's sheets keyed by
// "<at-rule prelude>|<selector>", a repeat merging last-wins as the cascade would at equal specificity,
// with source order kept beside it so a real cascade reorder still shows.
import { readFileSync } from 'node:fs';
import { dirname, relative, resolve } from 'node:path';

// The length of the atomic token starting at i, or -1 when it is not one: comments, strings, unquoted url()
// — which may hold a `;`, as data: URIs do — and `\` escapes are consumed whole, so none looks like syntax.
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

export function walk(css, from, to, context, out) {
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

export function newInventory() {
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
export function sheetsOf(htmlPath, root) {
  const sheets = [];
  for (const m of readFileSync(htmlPath, 'utf8').matchAll(/<link\b[^>]*>|<style\b[^>]*>([\s\S]*?)<\/style>/gi)) {
    if (/^<style/i.test(m[0])) { sheets.push({ name: `${relative(root, htmlPath)} <style>`, css: m[1] }); continue; }
    if (!/rel\s*=\s*["']?\s*stylesheet\b/i.test(m[0])) continue;
    const href = m[0].match(/href\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i);
    const url = href && (href[1] ?? href[2] ?? href[3]);
    if (!url || /^(https?:)?\/\/|^data:/i.test(url)) continue;   // only sheets that live in the tree
    const file = resolve(dirname(htmlPath), url);
    sheets.push({ name: relative(root, file), css: readFileSync(file, 'utf8') });
  }
  return sheets;
}

// One document's sheets as { rules, order }.
export const inventory = (sheets) => {
  const out = newInventory();
  for (const sheet of sheets) walk(sheet.css, 0, sheet.css.length, '', out);
  return out.finish();
};

export const scan = (css) => inventory([{ css }]);
