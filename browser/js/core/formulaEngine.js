import { core } from './stencilCore.js';

// ── Formula transforms (pure logic) ─────────────────────────────
// Port of `core/parse/formulaParser.cpp`: a recursive-descent evaluator (NOT `new Function`/
// `eval`) understanding only `+ - * / ** ( )` over one variable, so server-supplied formulas
// stay inert. JS reference + fallback; wasm delegates to the same parser when loaded.
// Grammar: expr = term (('+'|'-') term)*; term = unary (('*'|'/') unary)*;
// unary = ('+'|'-') unary | power; power = primary ('**' unary)? (right-assoc);
// primary = '(' expr ')' | number | the bound variable. Syntax error ⇒ ok=false (invalid).
// Cap recursion depth so an adversarial deeply-nested input (thousands of '(' or unary signs
// from untrusted layout JSON / console / co-edit) can't overflow the stack; past the cap the
// parse is invalid (→ identity). Must equal the core parser's kMaxDepth
// (core/parse/formulaParser.cpp) so wasm and this JS fallback agree op-for-op.
const MAX_DEPTH = 256;

class Evaluator {
  constructor(src, varName, varValue) {
    this.src = src;
    this.varName = varName;
    this.varValue = varValue;
    this.pos = 0;
    this.ok = true;
    this.depth = 0;
  }

  // Parse a full expression and require that all input was consumed.
  run() {
    const v = this.parseExpr();
    this.skipSpaces();
    if (!this.ok || this.pos !== this.src.length) return null;
    return v;
  }

  skipSpaces() {
    while (this.pos < this.src.length && /\s/.test(this.src[this.pos])) {
      this.pos += 1;
    }
  }

  peek() {
    this.skipSpaces();
    return this.pos < this.src.length ? this.src[this.pos] : '\0';
  }

  // Match a two-char operator like `**`.
  match2(a, b) {
    this.skipSpaces();
    if (this.pos + 1 < this.src.length && this.src[this.pos] === a && this.src[this.pos + 1] === b) {
      this.pos += 2;
      return true;
    }
    return false;
  }

  match(a) {
    this.skipSpaces();
    if (this.pos < this.src.length && this.src[this.pos] === a) {
      this.pos += 1;
      return true;
    }
    return false;
  }

  parseExpr() {
    if (++this.depth > MAX_DEPTH) { this.ok = false; this.depth -= 1; return 0; }
    let v = this.parseTerm();
    while (this.ok) {
      if (this.match('+')) v += this.parseTerm();
      else if (this.match('-')) v -= this.parseTerm();
      else break;
    }
    this.depth -= 1;
    return v;
  }

  parseTerm() {
    // '**' is consumed inside parsePower (reached via parseUnary), so the cursor never sits
    // on '**' when this loop tests for '*'.
    let v = this.parseUnary();
    while (this.ok) {
      if (this.match('*')) v *= this.parseUnary();
      else if (this.match('/')) v /= this.parseUnary();
      else break;
    }
    return v;
  }

  parseUnary() {
    if (++this.depth > MAX_DEPTH) { this.ok = false; this.depth -= 1; return 0; }
    this.skipSpaces();
    let result;
    if (this.match('+')) result = this.parseUnary();
    else if (this.match('-')) result = -this.parseUnary();
    else result = this.parsePower();
    this.depth -= 1;
    return result;
  }

  parsePower() {
    const base = this.parsePrimary();
    if (this.match2('*', '*')) {
      const exp = this.parseUnary(); // right-associative: 2 ** 3 ** 2
      return Math.pow(base, exp);
    }
    return base;
  }

  parsePrimary() {
    if (this.match('(')) {
      const v = this.parseExpr();
      if (!this.match(')')) this.ok = false;
      return v;
    }
    const c = this.peek();
    if ((c >= '0' && c <= '9') || c === '.') {
      return this.parseNumber();
    }
    if (isAlpha(c)) {
      return this.parseIdentifier();
    }
    this.ok = false;
    return 0;
  }

  parseNumber() {
    this.skipSpaces();
    const start = this.pos;
    while (this.pos < this.src.length && ((this.src[this.pos] >= '0' && this.src[this.pos] <= '9') || this.src[this.pos] === '.')) {
      this.pos += 1;
    }
    // optional exponent: e / E [+/-] digits
    if (this.pos < this.src.length && (this.src[this.pos] === 'e' || this.src[this.pos] === 'E')) {
      const save = this.pos;
      this.pos += 1;
      if (this.pos < this.src.length && (this.src[this.pos] === '+' || this.src[this.pos] === '-')) {
        this.pos += 1;
      }
      if (this.pos < this.src.length && this.src[this.pos] >= '0' && this.src[this.pos] <= '9') {
        while (this.pos < this.src.length && this.src[this.pos] >= '0' && this.src[this.pos] <= '9') {
          this.pos += 1;
        }
      } else {
        this.pos = save; // not an exponent after all
      }
    }
    const text = this.src.slice(start, this.pos);
    // parseFloat mirrors C++ std::stod: it reads the longest numeric prefix (so "1.2.3" → 1.2,
    // matching the core) and yields NaN only when no number could be parsed at all.
    const value = parseFloat(text);
    if (!Number.isFinite(value)) {
      this.ok = false;
      return 0;
    }
    return value;
  }

  parseIdentifier() {
    this.skipSpaces();
    const start = this.pos;
    while (this.pos < this.src.length && isAlpha(this.src[this.pos])) {
      this.pos += 1;
    }
    const ident = this.src.slice(start, this.pos);
    // Only the single bound variable is allowed; any other name (a function such as `foo`,
    // or a stray identifier) is a parse error.
    if (ident.length === 1 && ident === this.varName) return this.varValue;
    this.ok = false;
    return 0;
  }
}

function isAlpha(c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

function isBlank(s) {
  return !s || !s.trim();
}

// Evaluate `expr` with the single variable bound to `value`. Returns a finite number or null.
function evaluate(expr, varName, value) {
  const result = new Evaluator(expr, varName, value).run();
  if (result === null || !Number.isFinite(result)) return null;
  return result;
}

export class FormulaEngine {
  // Validate a formula string — try evaluating it with the variable = 1.
  // Empty = valid (identity).
  validate = core.bind('formulaValidate', (expr, varName) => {
    if (isBlank(expr)) return true; // empty = valid (identity)
    return evaluate(expr, varName, 1) !== null;
  });

  // Apply formula transform to a coordinate value; returns original if
  // formulas are disabled, the expression is empty, or evaluation fails.
  apply = core.bind('formulaApply', (expr, varName, val, allowFormulas) => {
    if (!allowFormulas || isBlank(expr)) return val;
    const result = evaluate(expr, varName, val);
    return result !== null ? result : val;
  });
}
