// Port of core/script/templates.cpp — `@stencil` definitions and their expansion.
import { didYouMean, makeDiag, tokenOfStmt } from './diagnostics.js';
import {
  MAX_OPS, MAX_TEMPLATE_DEPTH, MAX_TEMPLATE_EXPANSIONS, isStencilUse, unquoteWord,
} from './types.js';
import { classifyWord } from './lexer.js';
import { gluedWords, parseIntClamped } from './values.js';

const wordCount = (name) => {
  let n = 1;
  for (const c of name) if (c === ' ') n += 1;
  return n;
};

/* Built once per script. A call tries at most `longestWords` prefixes, and looks one up only
 * when some name has its length, so a long call costs no more than its own words. */
export const templateIndex = (templates) => ({
  byName: new Map(templates.map((d, i) => [d.name, i])),
  nameLengths: new Set(templates.map((d) => d.name.length)),
  longestWords: templates.reduce((top, d) => Math.max(top, wordCount(d.name)), 0),
});

// The call's words, in order, with the literal 'stencil' already dropped; a length stays one word.
const callWords = (use) => gluedWords(use.args).slice(1);

// Longest defined name prefixing the word run; the candidate shrinks in place.
const resolveName = (words, index) => {
  let n = Math.min(words.length, index.longestWords);
  let candidate = words.slice(0, n).map((w) => unquoteWord(w.text)).join(' ');
  for (; n >= 1; n -= 1) {
    if (index.nameLengths.has(candidate.length)) {
      const idx = index.byName.get(candidate);
      if (idx !== undefined) return { idx, wordsUsed: n };
    }
    const drop = unquoteWord(words[n - 1].text).length + (n > 1 ? 1 : 0);
    candidate = candidate.slice(0, candidate.length - drop);
  }
  return { idx: -1, wordsUsed: 0 };
};

/* @n becomes the n-th argument, re-read by the lexer's word rules so '10%' is a NUMBER and a
 * UNIT again; a quoted argument stays one plain word. */
const fill = (param, arg, out) => {
  const text = unquoteWord(arg.text);
  const { kind, unitAt } = arg.kind === 'string'
    ? { kind: 'ident', unitAt: -1 } : classifyWord(arg.text);
  const filled = { ...param, text, kind: kind === 'number' || kind === 'color' ? kind : 'ident' };
  if (unitAt < 0) {
    out.push(filled);
    return;
  }
  out.push({ ...filled, text: text.slice(0, unitAt), len: unitAt });
  const unit = text.slice(unitAt);
  out.push({ ...param, kind: 'unit', text: unit, col: param.col + unitAt, len: unit.length });
};

// Every other token passes through; the copy remembers the outermost call that made it.
const substitute = (body, use, args) => {
  const call = use.call ?? { line: use.line, col: use.col, len: use.len };
  const out = { ...body, args: [], call };
  let bad = null;
  for (const t of body.args) {
    if (t.kind !== 'param') {
      out.args.push(t);
      continue;
    }
    const n = parseIntClamped(t.text.slice(1));
    if (n < 1 || n > args.length) {
      bad = t;
      continue;
    }
    fill(t, args[n - 1], out.args);
  }
  return { stmt: out, bad };
};

/* Expands one `@use stencil <words> [args…]` into the referenced body. Nested uses expand
 * too, capped at MAX_TEMPLATE_DEPTH; a template that reaches itself hits that cap. `budget`
 * counts the whole script's tree: a body of nothing but nested uses trips no other cap. */
export const expandStencilUse = (use, templates, index, depth, budget, out, diags) => {
  if (depth > MAX_TEMPLATE_DEPTH) {
    diags.push(makeDiag('error', 'E_TEMPLATE_RECURSION', tokenOfStmt(use),
      `templates nest more than ${MAX_TEMPLATE_DEPTH} deep — is one using itself?`));
    return false;
  }
  budget.used += 1;
  if (budget.used > MAX_TEMPLATE_EXPANSIONS) {
    diags.push(makeDiag('error', 'E_LIMIT_OPS', tokenOfStmt(use), 'the script has too many ops'));
    return false;
  }

  const words = callWords(use);
  if (words.length === 0) {
    diags.push(makeDiag('error', 'E_ARG_COUNT', tokenOfStmt(use), "'@use stencil' needs a template name"));
    return false;
  }

  const { idx, wordsUsed } = resolveName(words, index);
  if (idx < 0) {
    const whole = words.map((w) => unquoteWord(w.text)).join(' ');
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
    const { stmt, bad } = substitute(st, use, args);
    if (bad) {
      diags.push(makeDiag('error', 'E_TEMPLATE_PARAM_INDEX', bad,
        `'${bad.text}' is outside this template's ${arity} argument(s)`));
      return false;
    }
    if (stmt.directive === 'use' && isStencilUse(stmt)) {
      if (!expandStencilUse(stmt, templates, index, depth + 1, budget, out, diags)) return false;
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
