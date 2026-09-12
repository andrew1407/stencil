import { core } from './stencilCore.js';

// Port of core/parse/formulaParser.cpp: a recursive-descent evaluator (never `new
// Function`/`eval`) over `+ - * / ** ( )` and one variable, so server-supplied formulas stay
// inert. Syntax error ⇒ invalid (→ identity). MAX_DEPTH caps recursion against adversarial
// nesting and must equal the core parser's MAX_DEPTH so wasm and this fallback agree.
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

// Requires that all input was consumed.
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
// '**' is consumed inside parsePower, so the cursor never sits on '**' here.
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
      const exp = this.parseUnary();
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
        this.pos = save;
      }
    }
    const text = this.src.slice(start, this.pos);
// parseFloat mirrors C++ std::stod: the longest numeric prefix ("1.2.3" → 1.2), NaN only
// when nothing parsed.
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
// Only the single bound variable; any other name is a parse error.
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

// A finite number or null.
function evaluate(expr, varName, value) {
  const result = new Evaluator(expr, varName, value).run();
  if (result === null || !Number.isFinite(result)) return null;
  return result;
}

export class FormulaEngine {
// Empty = valid (identity).
  validate = core.bind('formulaValidate', (expr, varName) => {
    if (isBlank(expr)) return true;
    return evaluate(expr, varName, 1) !== null;
  });

// Returns the original when formulas are off, the expression is empty, or evaluation fails.
  apply = core.bind('formulaApply', (expr, varName, val, allowFormulas) => {
    if (!allowFormulas || isBlank(expr)) return val;
    const result = evaluate(expr, varName, val);
    return result !== null ? result : val;
  });
}
