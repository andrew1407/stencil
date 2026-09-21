// ── Arithmetic in numeric inputs: the evaluator ─────────────────────────────
// Byte-pinned PORT of browser/js/ui/control/numericExpr.js. Pure — no DOM; operators match the core.

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
  // unary := ('+'|'-') unary | power — ABOVE power, so "-2 ** 2" is -(2**2), exactly what
  // core/parse/formulaParser does (parseUnary → parsePower, whose exponent is a parseUnary).
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

// A leading *, / or ** applies to the value already in the field, so "* 9" on 3 gives 27. A
// leading + or - is NOT: "-5" has to keep meaning negative five.
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