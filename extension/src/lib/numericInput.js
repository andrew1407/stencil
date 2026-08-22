// ── Arithmetic in numeric inputs ────────────────────────────────────────────
// The extension's numeric fields (the popup/sidepanel size filters, the crop page's
// custom page W/H) accept a small arithmetic expression instead of a bare number:
// type "45 + 9" and commit to get 54, or "* 9" against a current value of 3 to get 27.
//
// `<input type="number">` DISCARDS any non-numeric text (value reads back ""), so
// enhance() flips the element to type="text" + inputmode="decimal" at runtime and
// re-implements ArrowUp/ArrowDown stepping and min/max clamping. The markup keeps
// type="number" (and its step/min/max attributes) — a runtime upgrade, so existing
// `el.value` reads still see a plain number.
//
// A VERBATIM port of browser/js/ui/numericInput.js (same operator set as
// core/parse/formulaParser: + - * / ** and parens, ** right-associative, no eval).
// Change one, change the other — the tests on both sides assert the same cases.

// ── Evaluator (pure — no DOM, so Node tests import it directly) ──────────────

// Tokenize: numbers, operators, parens. Returns null on any unknown character.
const tokenize = (src) => {
  const out = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === ' ' || c === '\t') { i++; continue; }
    if (c >= '0' && c <= '9') {
      let j = i;
      while (j < src.length && src[j] >= '0' && src[j] <= '9') j++;
      if (src[j] === '.') { j++; while (j < src.length && src[j] >= '0' && src[j] <= '9') j++; }
      out.push({ t: 'num', v: Number(src.slice(i, j)) });
      i = j;
      continue;
    }
    if (c === '.') {                       // a bare ".5"
      let j = i + 1;
      while (j < src.length && src[j] >= '0' && src[j] <= '9') j++;
      if (j === i + 1) return null;
      out.push({ t: 'num', v: Number(src.slice(i, j)) });
      i = j;
      continue;
    }
    if (c === '*' && src[i + 1] === '*') { out.push({ t: 'op', v: '**' }); i += 2; continue; }
    if (c === '^') { out.push({ t: 'op', v: '**' }); i++; continue; }   // a friendlier alias
    if ('+-*/'.includes(c)) { out.push({ t: 'op', v: c }); i++; continue; }
    if (c === '(' || c === ')') { out.push({ t: c }); i++; continue; }
    return null;                            // anything else → not an expression
  }
  return out;
};

// Recursive descent over the token list. Throws on a malformed expression; the
// single caller below turns that into `null`.
const parse = (tokens) => {
  let pos = 0;
  const peek = () => tokens[pos];
  const fail = () => { throw new Error('bad expression'); };

  // expr := term (('+'|'-') term)*
  const expr = () => {
    let v = term();
    for (let t = peek(); t && t.t === 'op' && (t.v === '+' || t.v === '-'); t = peek()) {
      pos++;
      v = t.v === '+' ? v + term() : v - term();
    }
    return v;
  };
  // term := unary (('*'|'/') unary)*
  const term = () => {
    let v = unary();
    for (let t = peek(); t && t.t === 'op' && (t.v === '*' || t.v === '/'); t = peek()) {
      pos++;
      const rhs = unary();
      if (t.v === '/' && rhs === 0) fail();          // div-by-zero → invalid, like the core parser
      v = t.v === '*' ? v * rhs : v / rhs;
    }
    return v;
  };
  // unary := ('+'|'-') unary | power
  // Sits ABOVE power, so "-2 ** 2" is -(2**2) — exactly what core/parse/formulaParser
  // does (parseUnary → parsePower, and parsePower's exponent is itself a parseUnary).
  const unary = () => {
    const t = peek();
    if (t && t.t === 'op' && (t.v === '+' || t.v === '-')) {
      pos++;
      const v = unary();
      return t.v === '-' ? -v : v;
    }
    return power();
  };
  // power := primary ('**' unary)?   — right-associative
  const power = () => {
    const base = primary();
    const t = peek();
    if (t && t.t === 'op' && t.v === '**') { pos++; return base ** unary(); }
    return base;
  };
  const primary = () => {
    const t = peek();
    if (!t) fail();
    if (t.t === 'num') { pos++; return t.v; }
    if (t.t === '(') {
      pos++;
      const v = expr();
      if (!peek() || peek().t !== ')') fail();
      pos++;
      return v;
    }
    return fail();
  };

  const value = expr();
  if (pos !== tokens.length) fail();          // trailing junk
  return value;
};

// A leading *, / or ** means "apply this to the value already in the field", so
// "* 9" on 3 gives 27. A leading + or - is NOT treated that way: "-5" has to keep
// meaning negative five, which is what someone typing into a number field expects.
const CONTINUES_CURRENT = /^\s*(\*\*|\^|[*/])/;

/**
 * Evaluate what the user typed into a numeric field.
 * @param {string} text    the raw field text
 * @param {number} [current] the field's value before editing (for "* 9" style input)
 * @returns {number|null} a finite number, or null when the text isn't a valid expression
 */
export const evalNumericExpression = (text, current = 0) => {
  if (typeof text !== 'string') return null;
  const trimmed = text.trim();
  if (!trimmed) return null;
  const src = CONTINUES_CURRENT.test(trimmed) ? `${Number(current) || 0}${trimmed}` : trimmed;
  const tokens = tokenize(src);
  if (!tokens || !tokens.length) return null;
  try {
    const v = parse(tokens);
    return Number.isFinite(v) ? v : null;
  } catch {
    return null;                              // malformed → caller keeps the old value
  }
};

// ── DOM wiring ──────────────────────────────────────────────────────────────

const num = (v, fallback) => (v === '' || v == null || Number.isNaN(Number(v)) ? fallback : Number(v));

// Typing pauses for this long and the field commits itself; Enter/blur cancel the timer
// and commit at once. Long enough to think mid-expression ("45 + " … "9") without the
// field applying a half-written value under you.
export const COMMIT_DEBOUNCE_MS = 1200;

// Round to the precision implied by `step` so 0.1-step fields don't accumulate
// binary-float dust ("29.700000000000003") after a few keyboard steps.
const quantize = (value, step) => {
  const decimals = (String(step).split('.')[1] || '').length;
  return decimals ? Number(value.toFixed(decimals)) : value;
};

const clamp = (value, el) => {
  const lo = num(el.getAttribute('min'), -Infinity);
  const hi = num(el.getAttribute('max'), Infinity);
  return Math.min(hi, Math.max(lo, value));
};

/**
 * Upgrade one numeric input in place. Idempotent — re-enhancing is a no-op.
 * Commits on Enter and on blur; Escape restores the last committed value.
 * A cleared or half-written field is left alone until editing ends, so the
 * text you are still typing is never replaced mid-edit.
 */
export const enhanceNumericInput = (el) => {
  if (!el || el.dataset.numericEnhanced) return;
  el.dataset.numericEnhanced = '1';
  // Remember what the field was, so `type` reads and CSS keep working, then hand
  // the element a text type so arithmetic survives being typed.
  el.dataset.numericStep = el.getAttribute('step') || '1';
  el.type = 'text';
  el.inputMode = 'decimal';
  el.autocomplete = 'off';

  let committed = el.value;
  let timer = null;
  let emitting = false;   // true only while commit() re-emits the settled value
  const cancelPending = () => { if (timer) { clearTimeout(timer); timer = null; } };

  // `final` says editing has ended (Enter / blur / arrow step). The idle timer passes
  // false: a half-written field there means "still typing" and is left exactly as typed;
  // only when editing really ends does an unparseable field fall back to the last good value.
  const commit = ({ final = true } = {}) => {
    cancelPending();
    const step = el.dataset.numericStep;
    const value = evalNumericExpression(el.value, num(committed, 0));
    if (value == null) {
      if (final) el.value = committed;                     // unparseable → put it back
      return;
    }
    el.value = String(quantize(clamp(value, el), step));
    if (el.value === committed) return;                     // nothing actually changed
    committed = el.value;
    // Same events the native control fired, so every existing listener still works.
    emitting = true;
    try {
      el.dispatchEvent(new Event('input', { bubbles: true }));
      el.dispatchEvent(new Event('change', { bubbles: true }));
    } finally {
      emitting = false;
    }
  };

  const step = (dir) => {
    const s = Number(el.dataset.numericStep) || 1;
    const base = evalNumericExpression(el.value, num(committed, 0));
    const next = quantize(clamp((base == null ? num(committed, 0) : base) + dir * s, el), el.dataset.numericStep);
    el.value = String(next);
    commit();
  };

  // Raw keystrokes never reach the app's binders: they assume a parseable number on every
  // event, and half-typed text ("9 +") would read as NaN and clamp the field out from
  // under the user. Instead each keystroke restarts the idle timer — stop typing and the
  // field applies itself. A plain "8" takes the same path, so both behave identically.
  el.addEventListener('input', (e) => {
    if (emitting) return;                 // our own settled value — let it through
    e.stopImmediatePropagation();
    cancelPending();
    timer = setTimeout(() => commit({ final: false }), COMMIT_DEBOUNCE_MS);
  }, true);

  el.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowUp') { e.preventDefault(); step(1); }
    else if (e.key === 'ArrowDown') { e.preventDefault(); step(-1); }
    // Enter cancels the pending timer and applies immediately (commit does the cancel).
    else if (e.key === 'Enter') { e.preventDefault(); commit(); }
    else if (e.key === 'Escape') { cancelPending(); el.value = committed; el.blur(); }
  });
  el.addEventListener('blur', () => commit());
  // Something else (the app syncing state back) wrote a clean value in — adopt it
  // as the new baseline so Escape/relative edits work from what's on screen.
  el.addEventListener('change', () => {
    if (document.activeElement !== el && evalNumericExpression(el.value) != null) committed = el.value;
  });
};

/** Enhance every numeric input under `root` (safe to call repeatedly). */
export const enhanceNumericInputs = (root = document) => {
  if (!root || typeof root.querySelectorAll !== 'function') return;
  for (const el of root.querySelectorAll('input[type="number"]')) enhanceNumericInput(el);
};

/**
 * Install a one-time observer so inputs rendered later (modals, panels, the
 * context menu) are upgraded as they appear. Returns the MutationObserver.
 */
export const watchNumericInputs = (root = document.body) => {
  enhanceNumericInputs(document);
  const obs = new MutationObserver((records) => {
    for (const rec of records)
      for (const node of rec.addedNodes) {
        if (node.nodeType !== 1) continue;
        if (node.matches?.('input[type="number"]')) enhanceNumericInput(node);
        else enhanceNumericInputs(node);
      }
  });
  obs.observe(root, { childList: true, subtree: true });
  return obs;
};
