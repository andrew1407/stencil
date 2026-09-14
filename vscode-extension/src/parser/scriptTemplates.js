// Port of core/script/scriptTemplates.cpp — `@stencil` definitions and their expansion.
import { didYouMean, makeDiag, tokenOfStmt } from './scriptDiagnostics.js';
import { MAX_OPS, MAX_TEMPLATE_DEPTH, isStencilUse, unquoteWord } from './scriptTypes.js';

// Name -> index, built once per script: resolveName walks a prefix at a time.
export const templateIndex = (templates) => new Map(templates.map((d, i) => [d.name, i]));

// The call's words, in order, with the literal 'stencil' already dropped.
const callWords = (use) => {
  const words = [];
  let skippedKeyword = false;
  for (const t of use.args) {
    if (t.kind === 'punct') continue;
    if (!skippedKeyword) { skippedKeyword = true; continue; }
    words.push(unquoteWord(t.text));
  }
  return words;
};

const wordCount = (name) => {
  let n = 1;
  for (const c of name) if (c === ' ') n += 1;
  return n;
};

/* Longest defined name that is a prefix of the word run. Only prefixes up to the longest name
 * there is are built, because no longer one can match: building every prefix made one call
 * cost the SQUARE of its word count. */
const resolveName = (words, byName) => {
  let longest = 0;
  for (const name of byName.keys()) longest = Math.max(longest, wordCount(name));

  const limit = Math.min(words.length, longest);
  const prefixes = [];
  let built = '';
  for (let i = 0; i < limit; i += 1) {
    built = built ? `${built} ${words[i]}` : words[i];
    prefixes.push(built);
  }
  for (let n = prefixes.length; n >= 1; n -= 1) {
    const idx = byName.get(prefixes[n - 1]);
    if (idx !== undefined) return { idx, wordsUsed: n };
  }
  return { idx: -1, wordsUsed: 0 };
};

// @1..@n in the body become the call's arguments; every other token passes through.
const substitute = (body, args) => {
  const out = { ...body, args: [] };
  let bad = null;
  for (const t of body.args) {
    if (t.kind !== 'param') {
      out.args.push(t);
      continue;
    }
    const n = parseInt(t.text.slice(1), 10);
    if (!Number.isFinite(n) || n < 1 || n > args.length) {
      bad = t;
      continue;
    }
    out.args.push({ ...t, text: args[n - 1], kind: 'ident' });
  }
  return { stmt: out, bad };
};

/* Expands one `@use stencil <words> [args…]` into the referenced body. Nested uses expand
 * too, capped at MAX_TEMPLATE_DEPTH; a template that reaches itself hits that cap. The
 * statement count is capped as well — the fan-out is what an op cap alone cannot bound. */
export const expandStencilUse = (use, templates, byName, depth, out, diags) => {
  if (depth > MAX_TEMPLATE_DEPTH) {
    diags.push(makeDiag('error', 'E_TEMPLATE_RECURSION', tokenOfStmt(use),
      `templates nest more than ${MAX_TEMPLATE_DEPTH} deep — is one using itself?`));
    return false;
  }

  const words = callWords(use);
  if (words.length === 0) {
    diags.push(makeDiag('error', 'E_ARG_COUNT', tokenOfStmt(use), "'@use stencil' needs a template name"));
    return false;
  }

  const { idx, wordsUsed } = resolveName(words, byName);
  if (idx < 0) {
    const whole = words.join(' ');
    const near = didYouMean(whole, templates.map((d) => d.name));
    diags.push(makeDiag('error', 'E_UNDEFINED_TEMPLATE', tokenOfStmt(use),
      `no template named '${whole}'${near ? ` — did you mean '${near}'?` : ''}`));
    return false;
  }

  const args = words.slice(wordsUsed);
  const { arity, name } = templates[idx];
  if (args.length !== arity) {
    diags.push(makeDiag('error', 'E_TEMPLATE_ARITY', tokenOfStmt(use),
      `template '${name}' takes ${arity} argument(s), got ${args.length}`));
    return false;
  }

  templates[idx].used = true;
  // `substitute` copies each statement, so the body is only ever read here.
  for (const st of templates[idx].body) {
    const { stmt, bad } = substitute(st, args);
    if (bad) {
      diags.push(makeDiag('error', 'E_TEMPLATE_PARAM_INDEX', bad,
        `'${bad.text}' is outside this template's ${arity} argument(s)`));
      return false;
    }
    if (stmt.directive === 'use' && isStencilUse(stmt)) {
      if (!expandStencilUse(stmt, templates, byName, depth + 1, out, diags)) return false;
      continue;
    }
    if (out.length >= MAX_OPS) {
      diags.push(makeDiag('error', 'E_LIMIT_OPS', tokenOfStmt(use), 'the script has too many ops'));
      return false;
    }
    out.push(stmt);
  }
  return true;
};

export const reportUnusedTemplates = (templates, diags) => {
  for (const d of templates) {
    if (d.used) continue;
    diags.push(makeDiag('warning', 'W_UNUSED_TEMPLATE',
      { line: d.line, col: d.col, len: d.len }, `template '${d.name}' is never used`));
  }
};
